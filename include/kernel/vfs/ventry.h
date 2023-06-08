//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_VFS_VENTRY_H
#define KERNEL_VFS_VENTRY_H

#include <vfs_types.h>
#include <vfs/vnode.h>

#define VN(ve) __type_checked(struct ventry *, ve, ((ve)->vn))
#define VE_OPS(ve) __type_checked(struct ventry *, ve, (ve)->ops)

static inline ventry_t *ve_getref(ventry_t *ve) __move {
  if (ve) ref_get(&ve->refcount);
  return ve;
}

static inline ventry_t *ve_moveref(__move ventry_t **ref) __move {
  ventry_t *ve = *ref;
  *ref = NULL;
  return ve;
}

// ===== ventry operations =====
//
// locking reference:
//   _ = no lock
//   l = vnode/ventry lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.
//
ventry_t *ve_alloc_linked(cstr_t name, vnode_t *vnode) __move;
void ve_release(__move ventry_t **entryref);
void ve_release_swap(__move ventry_t **entryref, __move ventry_t **newve);
void ve_link_vnode(ventry_t *ve, __move vnode_t *vn); // ve = _, vn = l
void ve_unlink_vnode(ventry_t *ve, vnode_t *vn); // ve = l, vn = l
void ve_shadow_mount(ventry_t *mount_ve, ventry_t *root_ve); // mount_ve = l, root_ve = l
void ve_unshadow_mount(ventry_t *mount_ve); // mount_ve = l
void ve_add_child(ventry_t *parent, ventry_t *child); // parent = l, child = _
void ve_remove_child(ventry_t *parent, ventry_t *child); // parent = l, child = l
void ve_destroy(__move ventry_t **veref); // ve = l
bool ve_syncvn(ventry_t *ve); // ve = l

hash_t ve_hash_cstr(ventry_t *ve, cstr_t str);
bool ve_cmp_cstr(ventry_t *ve, cstr_t str);

//
//

static inline bool ve_lock(ventry_t *ve) {
  if (V_ISDEAD(ve)) return false;
  mutex_lock(&ve->lock);
  ve_syncvn(ve);
  if (V_ISDEAD(ve)) {
    mutex_unlock(&ve->lock); return false;
  }
  return true;
}

static inline void ve_unlock(ventry_t *ve) {
  mutex_unlock(&ve->lock);
}

static inline void ve_unlock_release(__move ventry_t **ve) {
  mutex_unlock(&(*ve)->lock);
  ve_release(ve);
}

static inline void v_unlock_linked_vn(ventry_t *ve) { mutex_unlock(&VN(ve)->lock); }

static inline void assert_new_ventry_valid(ventry_t *ve) {
  ve_syncvn(ve);
  if (!VE_ISLINKED(ve)) {
    panic("ventry not linked - allocate with vn_alloc() or link an unlinked one with ve_link_vnode()");
  }
  if (!V_ISEMPTY(ve)) {
    panic("vnode is not empty - did you accidentally call vfs_add_vnode()?");
  }
  if (V_ISDIR(ve)) {
    if (ve->chld_count != 2) {
      panic("directory vnode is missing . and .. entries");
    }
  }
}

#endif
