//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include "ramfs.h"
#include "memfile.h"

#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
// #define DPRINTF(fmt, ...) kprintf("ramfs_vnops: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)


ssize_t ramfs_vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  return memfile_read(memf, off, kio);
}

ssize_t ramfs_vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  ssize_t res = memfile_write(memf, off, kio);
  if (res > 0) {
    // update both vnode and ramfs node size if write extended the file
    size_t new_size = off + res;
    if (new_size > vn->size) {
      vn->size = new_size;
    }
    if (new_size > node->size) {
      node->size = new_size;
    }
  }
  return res;
}

int ramfs_vn_getpage(vnode_t *vn, off_t off, __move page_t **result) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  page_t *page = memfile_getpage(memf, off);
  if (page) {
    *result = page;
    return 0;
  }
  return -EIO;
}

int ramfs_vn_falloc(vnode_t *vn, size_t len) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;

  // len is the delta size to add, so calculate the new absolute size
  size_t new_size = memf->size + len;

  int res;
  if ((res = memfile_falloc(memf, new_size)) < 0) {
    return res; // failed to resize the file
  }

  // update the vnode and ramfs node sizes
  vn->size = new_size;
  node->size = new_size;
  return 0;
}

//

int ramfs_vn_readlink(vnode_t *vn, struct kio *kio) {
  DPRINTF("readlink vn={:vn}\n", vn);
  ramfs_node_t *node = vn->data;

  kio_t tmp = kio_readonly_from_str(node->n_link);
  size_t res = kio_transfer(kio, &tmp);
  if (res != str_len(node->n_link)) {
    return -EIO;
  }
  return 0;
}

ssize_t ramfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  DPRINTF("readdir vn={:vn} off=%lld\n", vn, off);
  ramfs_node_t *node = vn->data;
  size_t nread = 0;

  if (off == 0) {
    // write the dot entry
    if (!kio_write_new_dirent(vn->id, off, V_DIR, cstr_new(".", 1), dirbuf))
      return 0;
    nread++;
    off++;
  }
  if (off == 1) {
    // write the dotdot entry
    if (!kio_write_new_dirent(vn->parent_id, off, V_DIR, cstr_new("..", 2), dirbuf))
      return (ssize_t) nread;
    nread++;
    off++;
  }

  // adjust offset to skip the dot and dotdot entries
  off -= 2;
  size_t i = 0;
  ramfs_dentry_t *dent = LIST_FIRST(&node->n_dir);
  while (dent) {
    // get to the right offset
    if (i < off) {
      dent = LIST_NEXT(dent, list);
      i++;
      continue;
    }

    cstr_t name = cstr_from_str(dent->name);
    if (!kio_write_new_dirent(dent->node->id, (off_t) i, dent->node->type, name, dirbuf))
      break;

    dent = LIST_NEXT(dent, list);
    nread++;
    off++;
    i++;
  }

  return (ssize_t) nread;
}

//

int ramfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result) {
  DPRINTF("lookup dir={:vn} name=\"{:cstr}\"\n", dir, &name);
  ramfs_node_t *dnode = dir->data;
  ramfs_dentry_t *dent = ramfs_lookup_dentry(dnode, name);
  if (!dent) {
    return -ENOENT;
  }

  ramfs_node_t *node = dent->node;

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, &make_vattr(node->type, node->mode));
  vn->data = node;
  vn->size = node->size;
  vn->mtime = node->mtime;
  vn->ops = node->ops; // a per-vnode ops table may be provided
  DPRINTF("lookup found node (%u:%u)\n", dir->vfs->id, node->id);

  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vn_create(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  DPRINTF("create dir={:vn} name=\"{:cstr} vattr={:va}\"\n", dir, &name, vattr);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the file node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  node->n_file = memfile_alloc(0);
  ramfs_add_dentry(dnode, dent);
  DPRINTF("create allocated node (%u:%u)\n", vfs->id, node->id);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vn_mknod(vnode_t *dir, cstr_t name, struct vattr *vattr, dev_t dev, __move ventry_t **result) {
  DPRINTF("mknod dir={:vn} name=\"{:cstr}\" vattr={:va} dev=%u\n", dir, &name, vattr, dev);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  if (!(vattr->mode & S_IFCHR) && !(vattr->mode & S_IFBLK)) {
    DPRINTF("only character and block devices are supported\n");
    return -EINVAL;
  }

  // create the device node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  ramfs_add_dentry(dnode, dent);
  DPRINTF("mknod allocated node (%u:%u)\n", vfs->id, node->id);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vn_symlink(vnode_t *dir, cstr_t name, struct vattr *vattr, cstr_t target, __move ventry_t **result) {
  DPRINTF("symlink dir={:vn} name=\"{:cstr}\" vattr={:va} target=\"{:cstr}\"\n", dir, &name, vattr, &target);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the symlink node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  node->n_link = str_from_cstr(target);
  ramfs_add_dentry(dnode, dent);
  DPRINTF("symlink allocated node (%u:%u)\n", vfs->id, node->id);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vn_hardlink(vnode_t *dir, cstr_t name, vnode_t *target, __move ventry_t **result) {
  DPRINTF("hardlink dir={:vn} name=\"{:cstr}\" target={:vn}\n", dir, &name, target);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *tnode = target->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the new entry
  ramfs_dentry_t *dent = ramfs_alloc_dentry(tnode, name);
  ramfs_add_dentry(dnode, dent);
  ventry_t *ve = ve_alloc_linked(name, target);
  ve->data = dent;

  *result = moveref(ve);
  return 0;
}

int ramfs_vn_unlink(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  DPRINTF("unlink dir={:vn} vn={:vn} ve={:ve}\n", dir, vn, ve);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *node = vn->data;
  ramfs_dentry_t *dent = ve->data;
  ramfs_remove_dentry(dnode, dent);
  return 0;
}

int ramfs_vn_mkdir(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  DPRINTF("mkdir dir={:vn} name=\"{:cstr}\" vattr={:va}\n", dir, &name, vattr);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the directory node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  ramfs_add_dentry(dnode, dent);
  DPRINTF("mkdir allocated node (%u:%u)\n", vfs->id, node->id);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vn_rmdir(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  DPRINTF("rmdir dir={:vn} vn={:vn} ve={:ve}\n", dir, vn, ve);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *node = vn->data;
  ramfs_dentry_t *dent = ve->data;
  ramfs_remove_dentry(dnode, dent);
  return 0;
}

//

int ramfs_vn_no_create(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  return -EPERM;
}

int ramfs_vn_no_mknod(vnode_t *dir, cstr_t name, struct vattr *vattr, dev_t dev, __move ventry_t **result) {
  return -EPERM;
}

int ramfs_vn_no_symlink(vnode_t *dir, cstr_t name, struct vattr *vattr, cstr_t target, __move ventry_t **result) {
  return -EPERM;
}

int ramfs_vn_no_hardlink(vnode_t *dir, cstr_t name, vnode_t *target, __move ventry_t **result) {
  return -EPERM;
}

int ramfs_vn_no_unlink(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  return -EPERM;
}

int ramfs_vn_no_mkdir(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  return -EPERM;
}

int ramfs_vn_no_rmdir(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  return -EPERM;
}

//

void ramfs_vn_cleanup(vnode_t *vn) {
  ramfs_node_t *node = moveptr(vn->data);
  if (!node)
    return;

  DPRINTF("vn_cleanup vn={:+vn} [ramfs_node=%p]\n", vn, node);
  if (node->type == V_REG) {
    // release file resources now
    memfile_t *memf = node->n_file;
    memfile_free(memf);
    node->n_file = NULL;
  } else if (node->type == V_LNK) {
    str_free(&node->n_link);
  }

  ramfs_free_node(node);
}

void ramfs_ve_cleanup(ventry_t *ve) {
  ramfs_dentry_t *dent = ve->data;
  if (!dent)
    return;

  DPRINTF("ve_cleanup ve={:+ve} [ramfs_dent=%p]\n", ve, dent);
  str_free(&dent->name);
  ramfs_free_dentry(dent);
  ve->data = NULL;
}
