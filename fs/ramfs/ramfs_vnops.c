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
#define DPRINTF(fmt, ...) kprintf("ramfs_vnops: " fmt, ##__VA_ARGS__)

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

static inline size_t kio_write_dirent(ino_t ino, enum vtype type, cstr_t name, kio_t *kio) {
  struct dirent dirent;
  dirent.d_ino = ino;
  dirent.d_type = vtype_to_dtype(type);
  dirent.d_reclen = sizeof(struct dirent) + cstr_len(name) + 1;
  dirent.d_namlen = cstr_len(name);

  if (kio_remaining(kio) < dirent.d_reclen) {
    return 0;
  }

  // write the entry
  kio_write_in(kio, &dirent, sizeof(struct dirent), 0); // write dirent
  kio_write_in(kio, cstr_ptr(name), dirent.d_namlen + 1, 0); // write name
  return dirent.d_reclen;
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

int ramfs_vn_map(vnode_t *vn, off_t off, vm_mapping_t *mapping) {
  ramfs_node_t *node = vn->data;
  memfile_t *memf = node->n_file;
  return memfile_map(memf, off, mapping);
}

//

int ramfs_vn_readlink(vnode_t *vn, struct kio *kio) {
  DPRINTF("readlink id=%u\n", vn->id);
  ramfs_node_t *node = vn->data;

  kio_t tmp = kio_readonly_from_str(node->n_link);
  size_t res = kio_transfer(kio, &tmp);
  if (res != str_len(node->n_link)) {
    return -EIO;
  }
  return 0;
}

ssize_t ramfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  DPRINTF("readdir id=%u\n", vn->id);
  ramfs_node_t *node = vn->data;
  size_t i = 0;

  if (off == 0) {
    // write the dot entry
    if (!kio_write_dirent(vn->id, V_DIR, cstr_new(".", 1), dirbuf))
      return 0;
    i++;
  }
  if (off <= 1) {
    // write the dotdot entry
    if (!kio_write_dirent(vn->parent_id, V_DIR, cstr_new("..", 2), dirbuf))
      return (ssize_t) i;
    i++;
  }

  ramfs_dentry_t *dent = LIST_FIRST(&node->n_dir);
  while (dent) {
    // get to the right offset
    if (i < off) {
      dent = LIST_NEXT(dent, list);
      i++;
      continue;
    }

    cstr_t name = cstr_from_str(dent->name);
    if (!kio_write_dirent(dent->node->id, dent->node->type, name, dirbuf))
      break;

    dent = LIST_NEXT(dent, list);
    i++;
  }

  return (ssize_t) i;
}

//

int ramfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result) {
  DPRINTF("lookup id=%u \"{:cstr}\"\n", dir->id, &name);
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

  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = ve_moveref(&ve);
  vn_release(&vn);
  return 0;
}

int ramfs_vn_create(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  DPRINTF("create id=%u \"{:cstr}\"\n", dir->id, &name);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the file node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  node->n_file = memfile_alloc(0);
  ramfs_add_dentry(dnode, dent);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = ve_moveref(&ve);
  vn_release(&vn);
  return 0;
}

int ramfs_vn_mknod(vnode_t *dir, cstr_t name, struct vattr *vattr, dev_t dev, __move ventry_t **result) {
  DPRINTF("mknod id=%u \"{:cstr}\"\n", dir->id, &name);
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

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = ve_moveref(&ve);
  vn_release(&vn);
  return 0;
}

int ramfs_vn_symlink(vnode_t *dir, cstr_t name, struct vattr *vattr, cstr_t target, __move ventry_t **result) {
  DPRINTF("symlink id=%u \"{:cstr}\" -> \"{:cstr}\"\n", dir->id, &name, &target);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the symlink node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  node->n_link = str_copy_cstr(target);
  ramfs_add_dentry(dnode, dent);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = ve_moveref(&ve);
  vn_release(&vn);
  return 0;
}

int ramfs_vn_hardlink(vnode_t *dir, cstr_t name, vnode_t *target, __move ventry_t **result) {
  DPRINTF("hardlink id=%u \"{:cstr}\" -> id=%u\n", dir->id, &name, target->id);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *tnode = target->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the new entry
  ramfs_dentry_t *dent = ramfs_alloc_dentry(tnode, name);
  ramfs_add_dentry(dnode, dent);
  ventry_t *ve = ve_alloc_linked(name, target);
  ve->data = dent;

  *result = ve_moveref(&ve);
  return 0;
}

int ramfs_vn_unlink(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  DPRINTF("unlink id=%u \"{:str}\"\n", dir->id, &ve->name);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *node = vn->data;
  ramfs_dentry_t *dent = ve->data;
  ramfs_remove_dentry(dnode, dent);
  return 0;
}

int ramfs_vn_mkdir(vnode_t *dir, cstr_t name, struct vattr *vattr, __move ventry_t **result) {
  DPRINTF("mkdir id=%u \"{:cstr}\"\n", dir->id, &name);
  vfs_t *vfs = dir->vfs;
  ramfs_node_t *dnode = dir->data;
  ramfs_mount_t *mount = dnode->mount;

  // create the directory node and entry
  ramfs_node_t *node = ramfs_alloc_node(mount, vattr);
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  ramfs_add_dentry(dnode, dent);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, vattr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  ve->data = dent;

  *result = ve_moveref(&ve);
  vn_release(&vn);
  return 0;
}

int ramfs_vn_rmdir(vnode_t *dir, vnode_t *vn, ventry_t *ve) {
  DPRINTF("rmdir id=%u \"{:str}\"\n", dir->id, &ve->name);
  ramfs_node_t *dnode = dir->data;
  ramfs_node_t *node = vn->data;
  ramfs_dentry_t *dent = ve->data;
  ramfs_remove_dentry(dnode, dent);
  return 0;
}

//

void ramfs_vn_cleanup(vnode_t *vn) {
  // DPRINTF("cleanup\n");
  ramfs_node_t *node = vn->data;
  if (!node)
    return;

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
  // DPRINTF("cleanup\n");
  ramfs_dentry_t *dent = ve->data;
  if (!dent)
    return;

  str_free(&dent->name);
  ramfs_free_dentry(dent);
}
