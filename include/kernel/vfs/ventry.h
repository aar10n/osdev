//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_VFS_VENTRY_H
#define KERNEL_VFS_VENTRY_H

#include <kernel/vfs_types.h>
#include <kernel/vfs/vnode.h>

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
void ve_shadow_mount(ventry_t *mount_ve, vnode_t *root_vn); // mount_ve = l, root_vn = _
/// Unshadows a mount ventry's existing vnode returning the old mount vnode.
__ref vnode_t *ve_unshadow_mount(ventry_t *mount_ve); // mount_ve = l
/// Replaces the existing root mount with a new vnode, finally stacking the old mount back on top.
void ve_replace_root(ventry_t *root_ve, ventry_t *newroot_ve); // root_ve = l, newroot_ve = l
/// Adds a child ventry to a parent ventry.
void ve_add_child(ventry_t *parent, ventry_t *child); // parent = l, child = _
/// Removes a child ventry from a parent ventry.
void ve_remove_child(ventry_t *parent, ventry_t *child); // parent = l, child = l
/// Synchronizes a ventry with its vnode. If the vnode is dead
bool ve_syncvn(ventry_t *ve); // ve = l
/// Computes the hash of a ventry.
void ve_hash(ventry_t *ve); // ve = _


void ve_cleanup(ventry_t **veref);
hash_t ve_hash_cstr(ventry_t *ve, cstr_t str);
bool ve_cmp_cstr(ventry_t *ve, cstr_t str);

//
//

// #define VE_DPRINTF(fmt, ...) kprintf("ventry: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define VE_DPRINTF(fmt, ...)

/// Returns a new reference to the ventry ve.
#define ve_getref(ve) ({ VE_DPRINTF("ve_getref {:+ve} refcount=%d", ve, (ve) ? ref_count(&(ve)->refcount)+1 : 0); _ve_getref(ve); })
/// Moves the ref out of veref and returns it.
#define ve_moveref(veref) _ve_moveref(veref) // ({ VE_DPRINTF("ve_moveref {:ve}", *(veref)); _ve_moveref(veref); })
/// Moves the ref out of veref and releases it.
#define ve_release(veref) ({ VE_DPRINTF("ve_release {:+ve} refcount=%d", *(veref), *(veref) ? (ref_count(&(*(veref))->refcount)-1) : 0); _ve_release(veref); })
/// Replaces the ref in veref with ve and releases the old ref.
#define ve_release_swap(veref, newref) ({ VE_DPRINTF("ve_release_swap {:+ve} refcount=%d -> {:+ve}", *(veref), *(veref) ? (ref_count(&(*(veref))->refcount)-1) : 0, *(newref)); _ve_release_swap(veref, newref); })
/// Locks the ventry and returns true if it is alive and false if it is dead.
#define ve_lock(ve) _ve_lock(ve, __FILE__, __LINE__)
/// Unlocks the ventry.
#define ve_unlock(ve) mtx_unlock(&(ve)->lock);
/// Unlocks the ventry in veref and releases the ref.
#define ve_unlock_release(veref) ({ mtx_unlock(&(*(veref))->lock); ve_release(veref); })


static inline __ref ventry_t *_ve_getref(ventry_t *ve) {
  if (ve) ref_get(&ve->refcount);
  return ve;
}

static inline __ref ventry_t *_ve_moveref(__move ventry_t **ref) {
  ventry_t *ve = *ref;
  *ref = NULL;
  return ve;
}

static inline void _ve_release(__move ventry_t **entryref) {
  ventry_t *ve = ve_moveref(entryref);
  if (ve && ref_put(&ve->refcount)) {
    ve_cleanup(&ve);
  }
}

static inline void _ve_release_swap(__move ventry_t **entryref, __move ventry_t **newve) {
  ventry_t *oldve = ve_moveref(entryref);
  if (oldve && ref_put(&oldve->refcount)) {
    ve_cleanup(&oldve);
  }
  *entryref = ve_moveref(newve);
}

static inline uint64_t ve_unique_id(ventry_t *ve) {
  return (uint64_t) ve->vfs_id | ((uint64_t) ve->id << 32);
}

static inline bool _ve_lock(ventry_t *ve, const char *file, int line) {
  if (V_ISDEAD(ve))
    return false;

  _mtx_wait_lock(&ve->lock, file, line);
  ve_syncvn(ve);
  if (V_ISDEAD(ve)) {
    _mtx_wait_unlock(&ve->lock, file, line);
    return false;
  }
  return true;
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
