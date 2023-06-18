//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vnode.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <murmur3.h>

#define ASSERT(x) kassert(x)

#define MURMUR3_SEED 0xDEADBEEF

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ventry: %s: " fmt, __func__, ##__VA_ARGS__)

static struct ventry_ops ve_default_ops = {
  .v_cleanup = NULL,
};

static hash_t ve_hash_default(cstr_t str) {
  uint64_t tmp[2] = {0, 0};
  murmur_hash_x86_128(cstr_ptr(str), (int) cstr_len(str), MURMUR3_SEED, tmp);
  return tmp[0] ^ tmp[1];
}

static bool ve_cmp_default(ventry_t *ve, cstr_t str) {
  return cstr_eq(cstr_from_str(ve->name), str);
}

static void ve_cleanup(ventry_t *ve) {
  // called when last reference is released
  ASSERT(ve->state == V_DEAD);
  DPRINTF("cleanup [id=%u]\n", ve->id);
  if (VE_OPS(ve)->v_cleanup)
    VE_OPS(ve)->v_cleanup(ve);

  vn_release(&ve->vn);
  ve_release(&ve->parent);
  str_free(&ve->name);
  kfree(ve);
}

//

ventry_t *ve_alloc_linked(cstr_t name, vnode_t *vnode) __move {
  ventry_t *entry = kmallocz(sizeof(ventry_t));
  entry->type = vnode->type;
  entry->state = V_EMPTY;
  entry->name = str_copy_cstr(name);
  if (vnode->vfs) {
    entry->ops = vnode->vfs->type->ventry_ops;
  } else {
    entry->ops = &ve_default_ops;
  }

  mutex_init(&entry->lock, MUTEX_REENTRANT);
  ref_init(&entry->refcount);

  ve_link_vnode(entry, vn_getref(vnode));
  return entry;
}

void ve_release(__move ventry_t **entryref) {
  if (*entryref) {
    if (ref_put(&(*entryref)->refcount, NULL)) {
      ve_cleanup(*entryref);
      *entryref = NULL;
    }
  }
}

void ve_release_swap(ventry_t **entryref, ventry_t **newve) {
  ventry_t *oldve = *entryref;
  *entryref = *newve;
  *newve = NULL;
  ve_release(&oldve);
}

void ve_link_vnode(ventry_t *ve, __move vnode_t *vn) {
  ASSERT(ve->type == vn->type);
  ASSERT(!VE_ISLINKED(ve))
  ve->flags |= VE_LINKED;
  ve->id = vn->id;
  ve->vn = vn;

  vn->nlink++;
  vn->flags |= VN_DIRTY;
}

void ve_unlink_vnode(ventry_t *ve, vnode_t *vn) {
  ASSERT(VE_ISLINKED(ve));
  ve->id = 0;
  ve->flags &= ~VE_LINKED;

  vn->nlink--;
  vn->flags |= VN_DIRTY;
  // release the vnode reference on cleanup
}

void ve_shadow_mount(ventry_t *mount_ve, ventry_t *root_ve) {
  ASSERT(VE_ISLINKED(mount_ve));
  vnode_t *root_vn = VN(root_ve);

  mount_ve->flags |= VN_MOUNT;
  // move the old vnode reference to the new vnode
  root_vn->v_shadow = vn_moveref(&mount_ve->vn);
  mount_ve->vn = vn_getref(root_vn);
}

void ve_unshadow_mount(ventry_t *mount_ve) {
  ASSERT(VE_ISLINKED(mount_ve));
  ASSERT(VN_ISROOT(VN(mount_ve)));
  if (VN(mount_ve)->v_shadow == NULL) {
    panic("no shadow vnode - tried to unshadow fs_root?");
  }

  vnode_t *root_vn = vn_moveref(&mount_ve->vn);
  ASSERT(VN_ISMOUNT(root_vn));

  root_vn->flags &= ~VN_MOUNT;
  mount_ve->vn = vn_moveref(&root_vn->v_shadow);
  vn_release(&root_vn);
}

void ve_add_child(ventry_t *parent, ventry_t *child) {
  ASSERT(V_ISALIVE(parent));
  ASSERT(parent->type == V_DIR);
  ASSERT(child->parent == NULL);
  child->parent = ve_getref(parent);
  LIST_ADD(&parent->children, ve_getref(child), list);
  parent->chld_count++;
  if (VE_ISLINKED(child)) {
    VN(child)->parent_id = parent->id;
  }
}

void ve_remove_child(ventry_t *parent, ventry_t *child) {
  ASSERT(parent->type == V_DIR);
  ASSERT(child->parent == parent);
  ve_release(&child->parent);
  LIST_REMOVE(&parent->children, child, list);
  parent->chld_count--;
  ve_release(&child);
}

bool ve_syncvn(ventry_t *ve) {
  if (!VE_ISLINKED(ve))
    return false;

  vnode_t *vn = ve->vn;
  ASSERT(ve->id == vn->id);
  ASSERT(ve->type == vn->type);
  ve->state = vn->state;
  if (vn->vfs)
    ve->ops = vn->vfs->type->ventry_ops;
  return true;
}

void ve_hash(ventry_t *ve) {
  if (VE_OPS(ve)->v_hash)
    ve->hash = VE_OPS(ve)->v_hash(cstr_from_str(ve->name));
  else
    ve->hash = ve_hash_default(cstr_from_str(ve->name));
}

//

hash_t ve_hash_cstr(ventry_t *ve, cstr_t str) {
  if (VE_OPS(ve)->v_hash)
    return VE_OPS(ve)->v_hash(str);
  return ve_hash_default(str);
}

bool ve_cmp_cstr(ventry_t *ve, cstr_t str) {
  if (VE_OPS(ve)->v_cmp)
    return VE_OPS(ve)->v_cmp(ve, str);
  return ve_cmp_default(ve, str);
}
