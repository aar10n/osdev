//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#define PROCFS_INTERNAL
#include "procfs.h"

#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <fs/ramfs/ramfs.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)

#define PROCFS_OBJECT(vn) ((procfs_object_t *)((ramfs_node_t *)(vn)->data)->data)

extern struct file_ops procfs_file_ops;
extern struct vnode_ops procfs_vn_ops;

int procfs_vn_open(vnode_t *vn, int flags) {
  // called for static files and directories only
  return 0;
}

int procfs_vn_close(vnode_t *vn) {
  // called for static files and directories only
  return 0;
}

ssize_t procfs_vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  panic("procfs_vn_read: file operation should be called instead");
}

ssize_t procfs_vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  panic("procfs_vn_write: file operation should be called instead");
}

int procfs_vn_getpage(vnode_t *vn, off_t off, __move struct page **result) {
  panic("procfs_vn_getpage: file operation should be called instead");
}

int procfs_vn_falloc(vnode_t *vn, size_t len) {
  panic("procfs_vn_falloc: file operation should be called instead");
}

ssize_t procfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // delegate to ramfs
    return ramfs_vn_readdir(vn, off, dirbuf);
  }

  panic("procfs_vn_readdir: file operation should be called instead");
}

int procfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result) {
  procfs_object_t *obj = PROCFS_OBJECT(dir);
  if (!obj || obj->is_static) {
    // delegate to ramfs
    return ramfs_vn_lookup(dir, name, result);
  }

  // dynamic directory
  ASSERT(obj->ops && obj->ops->proc_lookup);
  procfs_object_t *child = NULL;
  int res = obj->ops->proc_lookup(obj, name, &child);
  if (res < 0) {
    return res;
  }

  // allocate the ramfs node and dentry for this file
  struct vattr attr = make_vattr(child->is_dir ? V_DIR : V_REG, child->mode);
  ramfs_mount_t *mount = dir->vfs->data;
  ramfs_node_t *node = ramfs_alloc_node(mount, &attr);
  node->data = child;
  node->ops = &procfs_vn_ops;
  ramfs_dentry_t *dent = ramfs_alloc_dentry(node, name);
  LIST_ADD(&child->nodes, node, list);

  // create the vnode and ventry
  vnode_t *vn = vn_alloc(node->id, &attr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(name, vn);
  // make sure the ventry is not cached or saved in-memory so we always
  // go through procfs_vn_lookup when accessing the directory
  ve->flags |= VE_NOCACHE | VE_NOSAVE;
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

void procfs_vn_alloc_file(vnode_t *vn, struct file *file) {
  file->ops = &procfs_file_ops;
}

void procfs_vn_cleanup(vnode_t *vn) {
  ramfs_node_t *ramfs_node = vn->data;
  procfs_object_t *obj = moveptr(ramfs_node->data);
  if (obj && !obj->is_ephemeral) {
    LIST_REMOVE(&obj->nodes, ramfs_node, list);
    ramfs_node->data = NULL;
  }
  if (obj->is_ephemeral) {
    if (obj->ops->proc_cleanup) {
      obj->ops->proc_cleanup(obj);
    }

    ASSERT(obj->data == NULL);
    str_free(&obj->path);
    kfree(obj);
  }
  ramfs_vn_cleanup(vn);
}
