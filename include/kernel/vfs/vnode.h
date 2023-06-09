//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#define __VNODE__
#ifndef KERNEL_VFS_VNODE_H
#define KERNEL_VFS_VNODE_H

#include <vfs_types.h>
#include <device.h>

#define VN_OPS(vn) __type_checked(struct vnode *, vn, (vn)->ops)

static inline vnode_t *vn_getref(vnode_t *vn) __move {
  ref_get(&vn->refcount);
  return vn;
}

static inline vnode_t *vn_moveref(__move vnode_t **vnref) __move {
  vnode_t *vn = *vnref;
  *vnref = NULL;
  return vn;
}

// ===== vnode operations =====
//
// locking reference:
//   _ = no lock
//   l = vnode/ventry lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.
// unless marked with a __move all pointer parameters are assumed to be references
// held by the caller.

vnode_t *vn_alloc_empty(enum vtype type) __move;
vnode_t *vn_alloc(id_t id, struct vattr *vattr) __move;
void vn_release(__move vnode_t **vnref);
void vn_setdirty(vnode_t *vn);
void vn_stat(vnode_t *vn, struct stat *statbuf); // vn = l

int vn_open(vnode_t *vn, int flags); // vn = _
int vn_close(vnode_t *vn); // vn = _
ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio); // vn = r
ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio); // vn = w
int vn_map(vnode_t *vn, off_t off, struct vm_mapping *mapping); // vn = _

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

static inline bool vn_lock(vnode_t *vn) {
  if (V_ISDEAD(vn)) return false;
  mutex_lock(&vn->lock);
  if (V_ISDEAD(vn)) {
    mutex_unlock(&vn->lock); return false;
  }
  return true;
}

static inline void vn_unlock(vnode_t *vn) {
  mutex_unlock(&vn->lock);
}

static inline bool vn_begin_data_read(vnode_t *vn) {
  if (V_ISDEAD(vn)) return false;
  rw_lock_read(&vn->data_lock);
  if (V_ISDEAD(vn)) {
    rw_unlock_read(&vn->data_lock); return false;
  }
  return true;
}

static inline void vn_end_data_read(vnode_t *vn) {
  rw_unlock_read(&vn->data_lock);
}

static inline bool vn_begin_data_write(vnode_t *vn) {
  if (V_ISDEAD(vn)) return false;
  rw_lock_write(&vn->data_lock);
  if (V_ISDEAD(vn)) {
    rw_unlock_write(&vn->data_lock); return false;
  }
  return true;
}

static inline void vn_end_data_write(vnode_t *vn) {
  rw_unlock_write(&vn->data_lock);
}


#endif
