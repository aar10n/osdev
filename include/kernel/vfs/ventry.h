//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_VFS_VENTRY_H
#define KERNEL_VFS_VENTRY_H

#include <kernel/vfs_types.h>
#include <kernel/vfs/vnode.h>
#include <kernel/sbuf.h>

#define VN(ve) __type_checked(struct ventry *, ve, ((ve)->vn))
#define VE_OPS(ve) __type_checked(struct ventry *, ve, (ve)->ops)

// ===== ventry operations =====
//
// locking reference:
//   _ = no lock
//   l = vnode/ventry lock
//   r = vnode data lock (read)
//   w = vnode data lock (write)
//
// comments after the function indicate the expected lock state of the parameters.

/// Allocates and links a new ventry.
__ref ventry_t *ve_alloc_linked(cstr_t name, vnode_t *vn);
/// Links a vnode to a ventry.
void ve_link_vnode(ventry_t *ve, __ref vnode_t *vn); // ve = _, vn = l
/// Unlinks a vnode from a ventry.
void ve_unlink_vnode(ventry_t *ve, vnode_t *vn); // ve = l, vn = l
/// Shadows a ventry's existing vnode with a new vnode.
void ve_shadow_mount(ventry_t *mount_ve, __ref vnode_t *root_vn); // mount_ve = l, root_vn = _
/// Unshadows a mount ventry's existing vnode returning the old mount vnode.
__ref vnode_t *ve_unshadow_mount(ventry_t *mount_ve); // mount_ve = l
/// Replaces the existing root mount with a new vnode, finally stacking the old mount back on top.
void ve_replace_root(ventry_t *root_ve, ventry_t *newroot_ve); // root_ve = l, newroot_ve = l
/// Adds a child ventry to a parent ventry.
void ve_add_child(ventry_t *parent, ventry_t *child); // parent = l, child = _
/// Removes a child ventry from a parent ventry.
void ve_remove_child(ventry_t *parent, ventry_t *child); // parent = l, child = l
// Writes the full path to a ventry into a string buffer.
ssize_t ve_get_path(ventry_t *ve, sbuf_t *buf); // ve = _
/// Synchronizes a ventry with its vnode. If the vnode is dead
bool ve_syncvn(ventry_t *ve); // ve = l
/// Computes the hash of a ventry.
void ve_hash(ventry_t *ve); // ve = _


void _ve_cleanup(ventry_t **veref);
hash_t ve_hash_cstr(ventry_t *ve, cstr_t str);
bool ve_cmp_cstr(ventry_t *ve, cstr_t str);

//
//

// #define VE_DPRINTF(fmt, ...) kprintf("ventry: %s: " fmt " [%s:%d]\n", __func__, ##__VA_ARGS__, __FILE__, __LINE__)
#define VE_DPRINTF(fmt, ...)

#define ve_getref(ve) ({ \
  ASSERT_IS_TYPE(ventry_t *, ve); \
  ventry_t *__ve = (ve); \
  __ve ? ref_get(&__ve->refcount) : NULL; \
  if (__ve) {            \
     VE_DPRINTF("getref {:+ve} [%d]", __ve, __ve->refcount); \
  } \
  __ve; \
})
#define ve_putref(veref) ({ \
  ASSERT_IS_TYPE(ventry_t **, veref); \
  ventry_t *__ve = *(veref); \
  *(veref) = NULL; \
  if (__ve) { \
    kassert(__ve->refcount > 0); \
    if (ref_put(&__ve->refcount)) { \
      VE_DPRINTF("putref {:+ve} [0]", __ve); \
      _ve_cleanup(&__ve); \
    } else {                \
      VE_DPRINTF("putref {:+ve} [%d]", __ve, __ve->refcount); \
    } \
  } \
})
#define ve_putref_swap(veref, newref) ({ \
  ASSERT_IS_TYPE(ventry_t **, veref); \
  ventry_t *__tmpve = moveref(*(veref)); \
  *(veref) = moveref(*(newref));         \
  ve_putref(&__tmpve); \
})
#define ve_lock(ve) ({ \
  ASSERT_IS_TYPE(ventry_t *, ve); \
  ventry_t *__ve = (ve); \
  mtx_lock(&__ve->lock); \
  bool _locked = true; \
  if (V_ISDEAD(__ve)) { \
    mtx_unlock(&__ve->lock);  \
    _locked = false; \
  } \
  _locked;\
})
#define ve_unlock(ve) ({ \
  ASSERT_IS_TYPE(ventry_t *, ve); \
  mtx_unlock(&(ve)->lock); \
})

#define ve_unlock_release(veref) ({ mtx_unlock(&(*(veref))->lock); ve_putref(veref); })
#define ve_lock_assert(ve, what) __type_checked(ventry_t *, ve, mtx_assert(&(ve)->lock, what))


static inline uint64_t ve_unique_id(ventry_t *ve) {
  return (uint64_t) ve->vfs_id | ((uint64_t) ve->id << 32);
}

static inline void assert_new_ventry_valid(ventry_t *ve) {
  ve_syncvn(ve);
  if (!VE_ISLINKED(ve)) {
    panic("ventry not linked - allocate with vn_alloc() or link an unlinked one with ve_link_vnode()");
  }
  if (!V_ISEMPTY(ve)) {
    panic("vnode is not empty - did you accidentally call vfs_add_node()?");
  }
  if (VN(ve)->id == 0) {
    panic("vnode id is 0 - did you forget to set it?");
  }
}

#endif
