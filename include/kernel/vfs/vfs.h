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
void vfs_cleanup(__move vfs_t **vfsref);

int vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve); // vfs = _, mount_ve = l
int vfs_unmount(vfs_t *vfs, ventry_t *mount_ve); // vfs = l, mount_ve = l
int vfs_sync(vfs_t *vfs); // vfs = l
int vfs_stat(vfs_t *vm, struct vfs_stat *stat); // vfs = l


//

// #define VFS_DPRINTF(fmt, ...) kprintf("vfs: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define VFS_DPRINTF(fmt, ...)

/// Returns a new reference to the vfs.
#define vfs_getref(vfs) ({ VFS_DPRINTF("vfs_getref id=%u<%p> refcount=%d", (vfs) ? (vfs)->id : 0, vfs, (vfs) ? ref_count(&(vfs)->refcount)+1 : 0); _vfs_getref(vfs); })
/// Moves the ref out of vfsref and returns it.
#define vfs_moveref(vfsref) _vfs_moveref(vfsref) // ({ VFS_DPRINTF("vfs_moveref id=%u", *(vfsref)); _vfs_moveref(vfsref); })
/// Moves the ref out of vfsref and releases it.
#define vfs_release(vfsref) ({ VFS_DPRINTF("vfs_release id=%u<%p> refcount=%d", (*(vfsref)) ? (*(vfsref))->id : 0, *(vfsref), (*(vfsref)) ? (ref_count(&(*(vfsref))->refcount)-1) : 0); _vfs_release(vfsref); })
/// Locks the vfs.
#define vfs_lock(vfs) _vfs_lock(vfs, __FILE__, __LINE__)
/// Unlocks the vfs.
#define vfs_unlock(vfs) mtx_unlock(&(vfs)->lock)


__ref static inline vfs_t *_vfs_getref(vfs_t *vfs) {
  if (vfs) ref_get(&vfs->refcount);
  return vfs;
}

__ref static inline vfs_t *_vfs_moveref(__move vfs_t **ref) {
  vfs_t *vfs = *ref;
  *ref = NULL;
  return vfs;
}

static inline void _vfs_release(__move vfs_t **ref) {
  vfs_t *vfs = vfs_moveref(ref);
  if (vfs && ref_put(&vfs->refcount)) {
    vfs_cleanup(&vfs);
  }
}

static inline bool _vfs_lock(vfs_t *vfs, const char *file, int line) {
  if (V_ISDEAD(vfs)) return false;
  _mtx_wait_lock(&vfs->lock, file, line);
  if (V_ISDEAD(vfs)) {
    _mtx_wait_unlock(&vfs->lock, file, line);
    return false;
  }
  return true;
}


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
