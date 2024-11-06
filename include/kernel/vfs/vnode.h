//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#define __VNODE__
#ifndef KERNEL_VFS_VNODE_H
#define KERNEL_VFS_VNODE_H

#include <kernel/vfs_types.h>
#include <kernel/device.h>

#define VN_OPS(vn) __type_checked(struct vnode *, vn, (vn)->ops)

// ===== vnode operations =====
//
// locking reference:
//   _ = no lock
//   l = vnode/ventry lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.
// unless marked with a __move or __ref all pointer parameters are assumed to be
// references held by the caller.

__ref vnode_t *vn_alloc_empty(enum vtype type);
__ref vnode_t *vn_alloc(id_t id, struct vattr *vattr);
__ref struct pgcache *vn_get_pgcache(vnode_t *vn); // vn = _
void vn_stat(vnode_t *vn, struct stat *statbuf); // vn = l
void vn_cleanup(__move vnode_t **vnref); // vnref = _

int vn_open(vnode_t *vn, int flags); // vn = _
int vn_close(vnode_t *vn); // vn = _
ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio); // vn = r
ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio); // vn = w
int vn_getpage(vnode_t *vn, off_t off, bool pgcache, __move page_t **result); // vn = _

int vn_load(vnode_t *vn); // vn = l
int vn_save(vnode_t *vn); // vn = l
int vn_readlink(vnode_t *vn, kio_t *kio); // vn = r
ssize_t vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf); // vn = r

int vn_lookup(ventry_t *dve, vnode_t *dvn, cstr_t name, __move ventry_t **result); // dve = l, dvn = r
int vn_create(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result); // dve = l, dvn = w
int vn_mknod(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, dev_t dev, __move ventry_t **result); // dve = l, dvn = w
int vn_symlink(ventry_t *dve, vnode_t *dvn, cstr_t name, cstr_t target, __move ventry_t **result); // dve = l, dvn = w
int vn_hardlink(ventry_t *dve, vnode_t *dvn, cstr_t name, vnode_t *target, __move ventry_t **result); // dve = l, dvn = w, target = l
int vn_unlink(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn); // dve = l, dvn = w, ve = l, vn = l
int vn_mkdir(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result); // dve = l, dvn = w
int vn_rmdir(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn); // dve = l, dvn = w, ve = l, vn = l
// int vn_rename(ventry_t *dve, ventry_t *ve, cstr_t name, ventry_t *new_dve, cstr_t new_name);

//
//

// #define VN_DPRINTF(fmt, ...) kprintf("vnode: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define VN_DPRINTF(fmt, ...)

/// Returns a new reference to the vnode vn.
#define vn_getref(vn) ({ VN_DPRINTF("vn_getref {:+vn} refcount=%d", vn, (vn) ? ref_count(&(vn)->refcount)+1 : 0); _vn_getref(vn); })
/// Moves the ref out of vnref and returns it.
#define vn_moveref(vnref) _vn_moveref(vnref) // ({ VN_DPRINTF("vn_moveref {:vn}", *(vnref)); _vn_moveref(vnref); })
/// Moves the ref of vnref and releases it.
#define vn_release(vnref) ({ VN_DPRINTF("vn_release {:+vn} refcount=%d", *(vnref), *(vnref) ? (ref_count(&(*(vnref))->refcount)-1) : 0); _vn_release(vnref); })
/// Locks the vnode vn.
#define vn_lock(vn) _vn_lock(vn, __FILE__, __LINE__)
/// Unlocks the vnode vn.
#define vn_unlock(vn) mtx_unlock(&(vn)->lock)

static inline vnode_t *_vn_getref(vnode_t *vn) __move {
  if (vn)
    ref_get(&vn->refcount);
  return vn;
}

static inline vnode_t *_vn_moveref(__move vnode_t **vnref) __move {
  vnode_t *vn = *vnref;
  *vnref = NULL;
  return vn;
}

static inline void _vn_release(__move vnode_t **vnref) {
  vnode_t *vn = vn_moveref(vnref);
  if (vn && ref_put(&vn->refcount)) {
    vn_cleanup(&vn);
  }
}

static inline bool _vn_lock(vnode_t *vn, const char *file, int line) {
  if (V_ISDEAD(vn))
    return false;

  _mtx_wait_lock(&vn->lock, file, line);
  if (V_ISDEAD(vn)) {
    _mtx_wait_unlock(&vn->lock, file, line);
    return false;
  }
  return true;
}

static inline bool vn_begin_data_read(vnode_t *vn) {
  if (V_ISDEAD(vn)) return false;
  rw_rlock(&vn->data_lock);
  if (V_ISDEAD(vn)) {
    rw_runlock(&vn->data_lock);
    return false;
  }
  return true;
}

static inline void vn_end_data_read(vnode_t *vn) {
  rw_runlock(&vn->data_lock);
}

static inline bool vn_begin_data_write(vnode_t *vn) {
  if (V_ISDEAD(vn)) return false;
  rw_wlock(&vn->data_lock);
  if (V_ISDEAD(vn)) {
    rw_wunlock(&vn->data_lock);
    return false;
  }
  return true;
}

static inline void vn_end_data_write(vnode_t *vn) {
  rw_wunlock(&vn->data_lock);
}

static inline vnode_t *vn_get_original_vnode(vnode_t *vn) {
  vnode_t *tmp = vn;
  while (tmp->v_shadow) tmp = tmp->v_shadow;
  return tmp;
}

#endif
