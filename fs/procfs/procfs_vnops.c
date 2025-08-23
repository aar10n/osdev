//
// Created by Aaron Gill-Braun on 2025-08-17.
//

#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <fs/ramfs/ramfs.h>

#define PROCFS_INTERNAL
#include "procfs.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)

#define PROCFS_OBJECT(vn) ((procfs_object_t *)((ramfs_node_t *)(vn)->data)->data)

extern struct vnode_ops procfs_vn_ops;

int procfs_vn_open(vnode_t *vn, int flags) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // ramfs file
    return 0;
  }
  
  // procfs object open
  if (obj->ops->pf_open) {
    return obj->ops->pf_open(obj, flags);
  }
  return 0;
}

int procfs_vn_close(vnode_t *vn) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // ramfs file
    return 0;
  }

  // procfs object close
  if (obj->ops->pf_close) {
    return obj->ops->pf_close(obj);
  }
  return 0;
}

ssize_t procfs_vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  DPRINTF("procfs_vn_read: vn=%p, off=%lld", vn, off);
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // ramfs file
    return ramfs_vn_read(vn, off, kio);
  }

  // procfs object read
  ASSERT(obj->ops && obj->ops->pf_read);
  return obj->ops->pf_read(obj, off, kio);
}

ssize_t procfs_vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // ramfs file
    return ramfs_vn_write(vn, off, kio);
  }

  // procfs object write
  if (obj->ops->pf_write) {
    return obj->ops->pf_write(obj, off, kio);
  }
  return -ENOSYS;
}

int procfs_vn_getpage(vnode_t *vn, off_t off, __move struct page **result) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj) {
    // regular ramfs file
    return ramfs_vn_getpage(vn, off, result);
  }
  
  // procfs objects don't support memory mapping
  return -ENOSYS;
}

int procfs_vn_falloc(vnode_t *vn, size_t len) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj) {
    // regular ramfs file
    return ramfs_vn_falloc(vn, len);
  }

  return 0;
}

ssize_t procfs_vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    // delegate to ramfs
    return ramfs_vn_readdir(vn, off, dirbuf);
  }

  // dynamic directory
  ASSERT(obj->ops && obj->ops->pf_readdir);
  size_t nread = 0;
  if (off == 0) {
    // write the "." entry
    struct dirent dot = dirent_make_entry(0, 0, V_DIR, cstr_make("."));
    if (!kio_write_dirent(0, 0, V_DIR, cstr_make("."), dirbuf)) {
      return 0; // buffer full
    }

    nread++;
    off++;
  }
  if (off == 1) {
    // write the ".." entry
    if (!kio_write_dirent(0, 0, V_DIR, cstr_make(".."), dirbuf)) {
      return (ssize_t) nread; // buffer full
    }

    nread++;
    off++;
  }

  ASSERT(off >= 2);
  while (kio_remaining(dirbuf) >= sizeof(struct dirent)) {
    struct dirent dirent;
    ssize_t res = obj->ops->pf_readdir(obj, off-2, &dirent);
    if (res < 0) {
      return res;
    } else if (res == 0) {
      // end of directory
      break;
    }

    // write the dirent to the buffer
    size_t n = kio_write_in(dirbuf, &dirent, res, 0);
    if (n != res) {
      break; // buffer full
    }

    nread++;
    off++;
  }

  return (ssize_t) nread;
}

int procfs_vn_lookup(vnode_t *dir, cstr_t name, __move ventry_t **result) {
  procfs_object_t *obj = PROCFS_OBJECT(dir);
  if (!obj || obj->is_static) {
    // delegate to ramfs
    return ramfs_vn_lookup(dir, name, result);
  }

  // dynamic directory
  ASSERT(obj->ops && obj->ops->pf_lookup);
  procfs_object_t *child = NULL;
  int res = obj->ops->pf_lookup(obj, name, &child);
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
  ve->flags |= VE_NOCACHE | VE_NOSAVE;
  ve->data = dent;

  *result = moveref(ve);
  vn_putref(&vn);
  return 0;
}

void procfs_vn_cleanup(vnode_t *vn) {
  ramfs_node_t *ramfs_node = vn->data;
  procfs_object_t *obj = moveptr(ramfs_node->data);
  if (obj && !obj->is_ephemeral) {
    LIST_REMOVE(&obj->nodes, ramfs_node, list);
    ramfs_node->data = NULL;
  }
  if (obj->is_ephemeral) {
    if (obj->ops->pf_cleanup) {
      obj->ops->pf_cleanup(obj);
    }

    ASSERT(obj->data == NULL);
    str_free(&obj->path);
    kfree(obj);
  }
  ramfs_vn_cleanup(vn);
}
