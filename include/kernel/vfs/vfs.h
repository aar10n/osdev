//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#ifndef KERNEL_VFS_VFS_H
#define KERNEL_VFS_VFS_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

#define VFS_OPS(vfs) ((vfs)->ops)

// ===== vfs operations =====
//
// locking reference:
//   _ = no lock
//   l = vfs/vnode/ventry lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.

__ref vfs_t *vfs_alloc(struct fs_type *type, int mount_flags);
void vfs_add_node(vfs_t *vfs, ventry_t *ve); // vfs = l, ve = _
void vfs_remove_node(vfs_t *vfs, vnode_t *vn); // vfs = l, vnode = l
void _vfs_cleanup(__move vfs_t **vfsref);

int vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve); // vfs = _, mount_ve = l
int vfs_unmount(vfs_t *vfs, ventry_t *mount_ve); // vfs = l, mount_ve = l
int vfs_sync(vfs_t *vfs); // vfs = l
int vfs_stat(vfs_t *vm, struct vfs_stat *stat); // vfs = l

//

//#define VFS_DPRINTF(fmt, ...) kprintf("vfs: %s: " fmt " [%s:%d]\n", __func__, ##__VA_ARGS__, __FILE__, __LINE__)
#define VFS_DPRINTF(fmt, ...)

#define vfs_getref(vfs) ({ \
  ASSERT_IS_TYPE(vfs_t *, vfs); \
  vfs_t *__vfs = (vfs); \
  __vfs ? ref_get(&__vfs->refcount) : NULL; \
  if (__vfs) {             \
    VFS_DPRINTF("getref id=%u<%p> [%d]", __vfs->id, __vfs, __vfs->refcount); \
  } \
  __vfs; \
})
#define vfs_putref(vfsref) ({ \
  ASSERT_IS_TYPE(vfs_t **, vfsref); \
  vfs_t *__vfs = *(vfsref); \
  *(vfsref) = NULL; \
  if (__vfs) { \
    kassert(__vfs->refcount > 0); \
    if (ref_put(&__vfs->refcount)) { \
      VFS_DPRINTF("putref id=%u<%p> [0]", __vfs->id, __vfs); \
      _vfs_cleanup(&__vfs); \
    } else {                  \
      VFS_DPRINTF("putref id=%u<%p> [%d]", __vfs->id, __vfs, __vfs->refcount); \
    }\
  } \
})
#define vfs_lock(vfs) ({ \
  ASSERT_IS_TYPE(vfs_t *, vfs); \
  vfs_t *__vfs = (vfs); \
  mtx_lock(&__vfs->lock); \
  bool _locked = true; \
  if (V_ISDEAD(__vfs)) { \
    mtx_unlock(&__vfs->lock);  \
    _locked = false; \
  } \
  _locked;\
})
#define vfs_unlock(vfs) ({ \
  ASSERT_IS_TYPE(vfs_t *, vfs); \
  mtx_unlock(&(vfs)->lock); \
})

#define vfs_lock_assert(vfs, what) ({ \
  ASSERT_IS_TYPE(vfs_t *, vfs); \
  mtx_assert(&(vfs)->lock, what); \
})
#define vfs_rwlock_assert(vfs, what) ({ \
  ASSERT_IS_TYPE(vfs_t *, vfs); \
  rw_assert(&(vfs)->op_lock, what); \
})

static inline bool vfs_begin_read_op(vfs_t *vfs) {
  if (V_ISDEAD(vfs)) return false;
  rw_rlock(&vfs->op_lock);
  if (V_ISDEAD(vfs)) {
    rw_runlock(&vfs->op_lock);
    return false;
  }
  return true;
}

static inline void vfs_end_read_op(vfs_t *vfs) {
  rw_runlock(&vfs->op_lock);
}

static inline bool vfs_begin_write_op(vfs_t *vfs) {
  if (V_ISDEAD(vfs)) return false;
  rw_wlock(&vfs->op_lock);
  if (V_ISDEAD(vfs)) {
    rw_wunlock(&vfs->op_lock);
    return false;
  }
  return true;
}

static inline void vfs_end_write_op(vfs_t *vfs) {
  rw_wunlock(&vfs->op_lock);
}

#endif
