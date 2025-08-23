//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/vfs/path.h>

#include <fs/ramfs/ramfs.h>

#define PROCFS_INTERNAL
#include "procfs.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("procfs: %s: " fmt, __func__, ##__VA_ARGS__)

#define HMAP_TYPE procfs_dir_t*
#include <hash_map.h>

// global procfs mount - only one procfs instance is supported
static hash_map_t *procfs_directories = NULL;
procfs_dir_t *global_procfs_root_dir = NULL;

//
// MARK: Public API
//

static procfs_object_t *procfs_lookup_object(cstr_t path) {
  ASSERT(!cstr_isnull(path));
  path_t p = path_from_cstr(path);
  if (!path_is_absolute(p)) {
    EPRINTF("procfs_lookup_object: path must be absolute: {:cstr}\n", &path);
    return NULL;
  } else if (path_is_slash(p)) {
    // root directory
    return global_procfs_root_dir->obj;
  }

  cstr_t name = cstr_from_path(path_basename(p));
  cstr_t dirpath = cstr_from_path(path_dirname(p));
  ASSERT(!cstr_eq_charp(name, "."));
  ASSERT(!cstr_eq_charp(name, ".."))

  // get the parent directory
  procfs_dir_t *dir = hash_map_get_cstr(procfs_directories, dirpath);
  if (!dir) {
    EPRINTF("procfs_lookup_object: parent directory does not exist: {:cstr}\n", &dirpath);
    return NULL;
  }

  // find the object in the parent directory
  procfs_dirent_t *entry = LIST_FIND(ent, &dir->entries, next, str_eq_cstr(ent->name, name));
  if (!entry) {
    EPRINTF("procfs_lookup_object: object does not exist in directory {:str}: {:cstr}\n", &dir->name, &name);
    return NULL;
  }

  return entry->obj;
}

static int procfs_register_object(procfs_object_t *obj) {
  ASSERT(!str_isnull(obj->path));
  cstr_t name = cstr_from_path(path_basename(path_from_str(obj->path)));
  cstr_t dirpath = cstr_from_path(path_dirname(path_from_str(obj->path)));
  ASSERT(!cstr_eq_charp(name, "."));
  ASSERT(!cstr_eq_charp(name, ".."))

  // get the parent directory
  procfs_dir_t *dir = hash_map_get_cstr(procfs_directories, dirpath);
  if (!dir) {
    EPRINTF("procfs_register_object: parent directory does not exist: {:cstr}\n", &dirpath);
    return -ENOENT;
  }

  // check for duplicate
  procfs_dirent_t *existing = LIST_FIND(ent, &dir->entries, next, str_eq_cstr(ent->name, name));
  if (existing) {
    EPRINTF("procfs_dir_add_object: object already exists in directory {:str}: {:cstr}\n", &dir->name, &name);
    return -EEXIST;
  }

  procfs_dir_t *thisdir = NULL;
  if (obj->is_dir) {
    // allocate a procfs_dir if its a directory object
    thisdir = kmallocz(sizeof(procfs_dir_t));
    thisdir->name = str_from_cstr(name);
    thisdir->obj = obj;
    thisdir->parent = dir;

    // add the new directory to the hash map
    hash_map_set_str(procfs_directories, str_dup(obj->path), thisdir);
  }

  // allocate a dirent for the object
  procfs_dirent_t *dirent = kmallocz(sizeof(procfs_dirent_t));
  dirent->name = str_from_cstr(name);
  dirent->obj = obj;
  dirent->dir = thisdir;

  // add it to the parent directory
  LIST_ADD(&dir->entries, dirent, next);

  // if the parent directory is already mounted we need to create this
  // object in all the associated ramfs nodes
  LIST_FOR_IN(ramfs_dir, &dir->obj->nodes, list) {
    enum vtype type = obj->is_dir ? V_DIR : V_REG;
    ramfs_node_t *node = ramfs_alloc_node(ramfs_dir->mount, &make_vattr(type, obj->mode));
    ramfs_dentry_t *dentry = ramfs_alloc_dentry(node, name);
    ramfs_add_dentry(ramfs_dir, dentry);
  }
  return 0;
}

int procfs_register_file(cstr_t path, procfs_ops_t *ops, void *data, int mode) {
  ASSERT(!cstr_isnull(path));
  ASSERT(ops != NULL);
  if (ops->pf_read == NULL) {
    EPRINTF("procfs_register_file: read operation is required\n");
  } else if (ops->pf_lookup != NULL) {
    EPRINTF("procfs_register_file: lookup operation is not allowed\n");
  } else if (ops->pf_readdir != NULL) {
    EPRINTF("procfs_register_file: readdir operation is not allowed\n");
  }

  path_t file_path = path_from_cstr(path);
  if (!path_is_absolute(file_path)) {
    EPRINTF("procfs_register_file: path must be absolute\n");
    return -EINVAL;
  } else if (path_is_slash(file_path)) {
    EPRINTF("procfs_register_file: invalid path /\n");
    return -EINVAL;
  }

  // create the procfs object
  procfs_object_t *obj = kmallocz(sizeof(procfs_object_t));
  obj->path = str_from_cstr(path);
  obj->ops = ops;
  obj->data = data;
  obj->mode = mode;
  obj->is_dir = false;
  obj->is_static = false;

  int res;
  if ((res = procfs_register_object(obj)) < 0) {
    kfree(obj);
    EPRINTF("procfs_register_file: failed to register object at path {:cstr}\n", &path);
    return res;
  }

  DPRINTF("registered {:cstr}\n", &path);
  return 0;
}

int procfs_register_dir(cstr_t path, procfs_ops_t *ops, void *data, int mode) {
  ASSERT(!cstr_isnull(path));
  ASSERT(ops != NULL);
  if (ops->pf_lookup == NULL) {
    EPRINTF("procfs_register_dir: lookup operation is required\n");
  } else if (ops->pf_readdir == NULL) {
    EPRINTF("procfs_register_dir: readdir operation is required\n");
  } else if (ops->pf_read != NULL) {
    EPRINTF("procfs_register_dir: read operation is not allowed\n");
  } else if (ops->pf_write != NULL) {
    EPRINTF("procfs_register_dir: write operation is not allowed\n");
  }

  path_t dir_path = path_from_cstr(path);
  if (!path_is_absolute(dir_path)) {
    EPRINTF("procfs_register_dir: path must be absolute\n");
    return -EINVAL;
  } else if (path_is_slash(dir_path)) {
    EPRINTF("procfs_register_dir: invalid path /\n");
    return -EINVAL;
  }

  // create the procfs object
  procfs_object_t *obj = kmallocz(sizeof(procfs_object_t));
  obj->path = str_from_cstr(path);
  obj->ops = ops;
  obj->data = data;
  obj->mode = mode;
  obj->is_dir = true;
  obj->is_static = false;

  int res;
  if ((res = procfs_register_object(obj)) < 0) {
    kfree(obj);
    EPRINTF("procfs_register_file: failed to register object at path {:cstr}\n", &path);
    return res;
  }

  DPRINTF("registered dir %s\n", path);
  return 0;
}

int procfs_register_static_dir(cstr_t path, int mode) {
  path_t dir_path = path_from_cstr(path);
  if (!path_is_absolute(dir_path)) {
    EPRINTF("procfs_register_static_dir: path must be absolute\n");
    return -EINVAL;
  } else if (path_is_slash(dir_path)) {
    EPRINTF("procfs_register_static_dir: invalid path /\n");
    return -EINVAL;
  }

  // create the procfs object
  procfs_object_t *obj = kmallocz(sizeof(procfs_object_t));
  obj->path = str_from_cstr(path);
  obj->ops = NULL;
  obj->data = NULL;
  obj->mode = mode;
  obj->is_dir = true;
  obj->is_static = true;

  int res;
  if ((res = procfs_register_object(obj)) < 0) {
    kfree(obj);
    EPRINTF("procfs_register_file: failed to register object at path {:cstr}\n", &path);
    return res;
  }

  DPRINTF("registered static dir %s\n", path);
  return 0;
}

int procfs_unregister(cstr_t path) {
  todo("procfs_unregister not implemented");
}


//
// MARK: Public API for procfs objects
//

procfs_object_t *procfs_ephemeral_object(cstr_t name, procfs_ops_t *ops, void *data, int mode, bool is_dir) {
  ASSERT(ops != NULL);
  if (is_dir) {
    ASSERT(ops->pf_readdir != NULL && "pf_readdir is required for directories");
    ASSERT(ops->pf_lookup != NULL && "pf_lookup is required for directories");
  } else {
    ASSERT(ops->pf_read != NULL && "pf_read is required for files");
    ASSERT(ops->pf_lookup == NULL && "pf_lookup is not allowed for files");
    ASSERT(ops->pf_readdir == NULL && "pf_readdir is not allowed for files");
  }

  // create the procfs object
  procfs_object_t *obj = kmallocz(sizeof(procfs_object_t));
  obj->path = str_from_cstr(name);
  obj->ops = ops;
  obj->data = data;
  obj->mode = mode;
  obj->is_dir = is_dir;
  obj->is_static = false; // ephemeral objects cannot be static
  obj->is_ephemeral = true;
  return obj;
}

cstr_t procfs_obj_name(procfs_object_t *obj) {
  path_t basename = path_basename(path_from_str(obj->path));
  return cstr_from_path(basename);
}

void *procfs_obj_data(procfs_object_t *obj) {
  return obj->data;
}

ssize_t procfs_obj_read_string(procfs_object_t *obj, off_t off, kio_t *kio, const char *str) {
  size_t len = strlen(str);
  if (off >= len) {
    return 0;
  }

  size_t to_read = min(len - off, kio_remaining(kio));
  return (ssize_t) kio_write_in(kio, str + off, to_read, 0);
}

// MARK: fs registration

struct vfs_ops procfs_vfs_ops = {
  .v_mount = procfs_vfs_mount,
  .v_unmount = ramfs_vfs_unmount,
  .v_stat = ramfs_vfs_stat,
  .v_cleanup = ramfs_vfs_cleanup,
};

struct vnode_ops procfs_vn_ops = {
  .v_open = procfs_vn_open,
  .v_close = procfs_vn_close,
  .v_read = procfs_vn_read,
  .v_write = procfs_vn_write,
  .v_getpage = procfs_vn_getpage,
  .v_falloc = procfs_vn_falloc,

  .v_readlink = ramfs_vn_readlink,
  .v_readdir = procfs_vn_readdir,

  .v_lookup = procfs_vn_lookup,
  .v_create = ramfs_vn_no_create,
  .v_mknod = ramfs_vn_no_mknod,
  .v_symlink = ramfs_vn_no_symlink,
  .v_hardlink = ramfs_vn_no_hardlink,
  .v_unlink = ramfs_vn_no_unlink,
  .v_mkdir = ramfs_vn_no_mkdir,
  .v_rmdir = ramfs_vn_no_rmdir,
  .v_cleanup = procfs_vn_cleanup,
};

struct ventry_ops procfs_ve_ops = {
  .v_cleanup = ramfs_ve_cleanup,
};

static fs_type_t procfs_type = {
  .name = "procfs",
  .vfs_ops = &procfs_vfs_ops,
  .vn_ops = &procfs_vn_ops,
  .ve_ops = &procfs_ve_ops,
};

static void procfs_static_init() {
  // a procfs filesystem is a modified ramfs filesystem with custom vnode
  // operations for dynamic files and directories. it disallows the creation
  // of all files through the fs interface, and will only ever host dynamic
  // objects (files/directories) and static directories registered through
  // the public procfs API.
  if (fs_register_type(&procfs_type) < 0) {
    panic("failed to register procfs type\n");
  }

  // allocate the procfs root directory and object
  procfs_object_t *root_object = kmallocz(sizeof(procfs_object_t));
  root_object->path = str_from_charp("/");
  root_object->is_dir = true;
  root_object->is_static = true;
  root_object->mode = 0755;

  global_procfs_root_dir = kmallocz(sizeof(procfs_dir_t));
  global_procfs_root_dir->obj = root_object;
  global_procfs_root_dir->name = str_from_charp("/");

  // create the procfs dir hashmap
  procfs_directories = hash_map_new();
  hash_map_set(procfs_directories, "/", global_procfs_root_dir);
}
STATIC_INIT(procfs_static_init);
