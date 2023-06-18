//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#ifndef KERNEL_VFS_VFS_H
#define KERNEL_VFS_VFS_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

#define VFS_OPS(vfs) ((vfs)->ops)

static inline vfs_t *vfs_getref(vfs_t *vfs) __move {
  if (vfs) ref_get(&vfs->refcount);
  return vfs;
}

static inline vfs_t *vfs_moveref(__move vfs_t **ref) __move {
  vfs_t *vfs = *ref;
  *ref = NULL;
  return vfs;
}

// ===== vfs operations =====
//
// locking reference:
//   _ = no lock
//   l = vnode/ventry/vfs lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.
//
vfs_t *vfs_alloc(struct fs_type *type, int flags) __move;
void vfs_release(__move vfs_t **ref);
void vfs_add_vnode(vfs_t *vfs, vnode_t *vnode); // vfs = l, vnode = _
void vfs_remove_vnode(vfs_t *vfs, vnode_t *vnode); // vfs = l, vnode = l

int vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve); // vfs = _, mount_ve = l
int vfs_unmount(vfs_t *vfs, ventry_t *mount); // vfs = l, mount = l
int vfs_sync(vfs_t *vfs); // vfs = l
int vfs_stat(vfs_t *vm, struct vfs_stat *stat); // vfs = l

// Locking functions

static inline bool vfs_lock(vfs_t *vfs) {
  if (V_ISDEAD(vfs)) return false;
  mutex_lock(&vfs->lock);
  if (V_ISDEAD(vfs)) {
    mutex_unlock(&vfs->lock);
    return false;
  }
  return true;
}

static inline void vfs_unlock(vfs_t *vfs) {
  mutex_unlock(&vfs->lock);
}

static inline bool vfs_begin_read_op(vfs_t *vfs) {
  if (V_ISDEAD(vfs)) return false;
  rw_lock_read(&vfs->op_lock);
  if (V_ISDEAD(vfs)) {
    rw_unlock_read(&vfs->op_lock);
    return false;
  }
  return true;
}

static inline void vfs_end_read_op(vfs_t *vfs) {
  rw_unlock_read(&vfs->op_lock);
}

static inline bool vfs_begin_write_op(vfs_t *vfs) {
  if (V_ISDEAD(vfs)) return false;
  rw_lock_write(&vfs->op_lock);
  if (V_ISDEAD(vfs)) {
    rw_unlock_write(&vfs->op_lock);
    return false;
  }
  return true;
}

static inline void vfs_end_write_op(vfs_t *vfs) {
  rw_unlock_write(&vfs->op_lock);
}

#endif
