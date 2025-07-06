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

static inline unsigned char vtype_to_dtype(enum vtype type) {
  switch (type) {
    case V_REG:
      return DT_REG;
    case V_DIR:
      return DT_DIR;
    case V_LNK:
      return DT_LNK;
    case V_CHR:
      return DT_CHR;
    case V_BLK:
      return DT_BLK;
    case V_FIFO:
      return DT_FIFO;
    case V_SOCK:
      return DT_SOCK;
    default:
      return DT_UNKNOWN;
  }
}

static inline size_t kio_write_dirent(ino_t ino, off_t off, enum vtype type, cstr_t name, kio_t *kio) {
  struct dirent dirent;
  dirent.d_ino = ino;
  dirent.d_off = off;
  dirent.d_type = vtype_to_dtype(type);
  dirent.d_reclen = sizeof(struct dirent);
  cstr_memcpy(name, dirent.d_name, 256);

  if (kio_remaining(kio) < dirent.d_reclen) {
    return 0;
  }

  return kio_write_in(kio, &dirent, sizeof(struct dirent), 0); // write dirent
}

static inline size_t kio_write_linux_dirent64(ino_t ino, off_t off, enum vtype type, cstr_t name, kio_t *kio) {
  struct linux_dirent64 dirent;
  dirent.d_ino = ino;
  dirent.d_off = off;
  dirent.d_type = vtype_to_dtype(type);
  dirent.d_reclen = offsetof(struct linux_dirent64, d_name) + cstr_len(name) + 1;

  size_t n = 0;
  n += kio_write_in(kio, &dirent, offsetof(struct linux_dirent64, d_name), 0); // write dirent
  n += kio_write_in(kio, cstr_ptr(name), cstr_len(name), 0); // write name
  n += kio_write_in(kio, "\0", 1, 0); // write null terminator
  return n;
}

//

ssize_t ramfs_vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  return memfile_read(memf, off, kio);
}

ssize_t ramfs_vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  return memfile_write(memf, off, kio);
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

  int res;
  if ((res = memfile_falloc(memf, len)) < 0) {
    return res; // failed to resize the file
  }
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
    if (!kio_write_dirent(vn->id, off, V_DIR, cstr_new(".", 1), dirbuf))
      return 0;
    nread++;
    off++;
  }
  if (off == 1) {
    // write the dotdot entry
    if (!kio_write_dirent(vn->parent_id, off, V_DIR, cstr_new("..", 2), dirbuf))
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
    if (!kio_write_dirent(dent->node->id, (off_t)i, dent->node->type, name, dirbuf))
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

void ramfs_vn_cleanup(vnode_t *vn) {
  ramfs_node_t *node = vn->data;
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
