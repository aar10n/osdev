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
// unless marked with a __ref all pointer parameters are assumed to be pointers to
// a reference held by the caller.

__ref vnode_t *vn_alloc_empty(enum vtype type);
__ref vnode_t *vn_alloc(id_t id, struct vattr *vattr);
__ref struct pgcache *vn_get_pgcache(vnode_t *vn); // vn = _
bool vn_isatty(vnode_t *vn); // vn = l
void _vn_cleanup(__move vnode_t **vnref); // vnref = _

int vn_open(vnode_t *vn, int flags); // vn = l
int vn_close(vnode_t *vn); // vn = l
int vn_getpage(vnode_t *vn, off_t off, bool cached, __move page_t **result); // vn = _
ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio); // vn = r
ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio); // vn = w
int vn_ioctl(vnode_t *vn, unsigned long request, void *arg); // vn = l
int vn_fallocate(vnode_t *vn, off_t length); // vn = w
void vn_stat(vnode_t *vn, struct stat *statbuf); // vn = l

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

//#define VN_DPRINTF(fmt, ...) kprintf("vnode: %s: " fmt " [%s:%d]\n", __func__, ##__VA_ARGS__, __FILE__, __LINE__)
#define VN_DPRINTF(fmt, ...)

#define vn_getref(vn) ({ \
  ASSERT_IS_TYPE(vnode_t *, vn); \
  vnode_t *__vn = (vn); \
  if (__vn) kassert(__vn->refcount > 0); \
  __vn ? ref_get(&__vn->refcount) : NULL; \
  if (__vn) {            \
     VN_DPRINTF("getref {:+vn} [%d]", __vn, __vn->refcount); \
  } \
  __vn; \
})
#define vn_putref(vnref) ({ \
  ASSERT_IS_TYPE(vnode_t **, vnref); \
  vnode_t *__vn = *(vnref); \
  *(vnref) = NULL; \
  if (__vn) { \
    kassert(__vn->refcount > 0); \
    if (ref_put(&__vn->refcount)) { \
      VN_DPRINTF("putref {:+vn} [0]", __vn); \
      _vn_cleanup(&__vn); \
    } else {                \
      VN_DPRINTF("putref {:+vn} [%d]", __vn, __vn->refcount); \
    } \
  } \
})
#define vn_lock(vn) ({ \
  ASSERT_IS_TYPE(vnode_t *, vn); \
  vnode_t *__vn = (vn); \
  mtx_lock(&__vn->lock); \
  bool _locked = true; \
  if (V_ISDEAD(__vn)) { \
    mtx_unlock(&__vn->lock);  \
    _locked = false; \
  } \
  _locked;\
})
#define vn_unlock(vn) ({ \
  ASSERT_IS_TYPE(vnode_t *, vn); \
  mtx_unlock(&(vn)->lock); \
})

#define vn_lock_assert(vn, what) __type_checked(vnode_t *, vn, mtx_assert(&(vn)->lock, what))
#define vn_rwlock_assert(vn, what) __type_checked(vnode_t *, vn, rw_assert(&(vn)->data_lock, what))

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
