//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vnode.h>

#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <murmur3.h>

#define MURMUR3_SEED 0xDEADBEEF

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("ventry: %s: " fmt, __func__, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)

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

//

__ref ventry_t *ve_alloc_linked(cstr_t name, vnode_t *vn) {
  ventry_t *entry = kmallocz(sizeof(ventry_t));
  entry->type = vn->type;
  entry->state = V_EMPTY;
  entry->name = str_from_cstr(name);
  if (vn->vfs) {
    entry->ops = vn->vfs->type->ve_ops;
  } else {
    entry->ops = &ve_default_ops;
  }

  mtx_init(&entry->lock, 0, "ventry_lock");
  ref_init(&entry->refcount);
  VE_DPRINTF("ref init {:+ve} [1]", entry);

  ve_link_vnode(entry, vn);
  ve_syncvn(entry);

  DPRINTF("allocated {:+ve} and linked to {:+vn}\n", entry, vn);
  return entry;
}

void ve_link_vnode(ventry_t *ve, vnode_t *vn) {
  ASSERT(ve->type == vn->type);
  ASSERT(!VE_ISLINKED(ve))
  vn->nlink++;
  vn->flags |= VN_DIRTY;

  ve->flags |= VE_LINKED;
  ve->id = vn->id;
  ve->vn = vn_getref(vn);
}

void ve_unlink_vnode(ventry_t *ve, vnode_t *vn) {
  DPRINTF("unlinked {:ve} from {:vn}\n", ve, vn);
  ve->flags &= ~VE_LINKED;
  vn->nlink--;
  vn->flags |= VN_DIRTY;
}

void ve_shadow_mount(ventry_t *mount_ve, __ref vnode_t *root_vn) {
  ASSERT(root_vn->v_shadow == NULL);
  ASSERT(mount_ve->chld_count == 0);

  root_vn->v_shadow = moveref(mount_ve->vn);
  root_vn->flags |= VN_ROOT;
  ve_putref(&mount_ve->mount);
  if (root_vn->vfs) {
    mount_ve->mount = ve_getref(root_vn->vfs->root_ve);
  }

  mount_ve->vn = moveref(root_vn);
  mount_ve->flags |= VE_MOUNT;
  ve_syncvn(mount_ve);
}

__ref vnode_t *ve_unshadow_mount(ventry_t *mount_ve) {
  if (!VN_ISROOT(VN(mount_ve))) {
    ASSERT(VN_ISROOT(VN(mount_ve)));
  }
  if (VN(mount_ve)->v_shadow == NULL) {
    panic("no shadow vnode - tried to unshadow fs_root?");
  }

  vnode_t *root_vn = moveref(mount_ve->vn);
  mount_ve->vn = moveref(root_vn->v_shadow);
  ve_putref(&mount_ve->mount);
  if (VN(mount_ve)->v_shadow == NULL) {
    // no more stacked mounts
    mount_ve->flags &= ~VE_MOUNT;
  } else {
    mount_ve->mount = ve_getref(VN(mount_ve)->vfs->root_ve);
  }

  ve_syncvn(mount_ve);
  return root_vn;
}

void ve_replace_root(ventry_t *root_ve, ventry_t *newroot_ve) {
  // unshadow the oldroot vnode mount temporarily
  __ref vnode_t *oldroot_vn = ve_unshadow_mount(root_ve);
  // unshadow the newroot vnode from its mount ventry
  __ref vnode_t *newroot_vn = ve_unshadow_mount(newroot_ve);

  // update the newroot root ventry parent ref
  ventry_t *newroot_root_ve = newroot_vn->vfs->root_ve;
  ve_putref(&newroot_root_ve->parent);
  newroot_root_ve->parent = ve_getref(root_ve);

  // stack the newroot vnode on top of the fs root vnode
  ve_shadow_mount(root_ve, moveref(newroot_vn));
  // now stack the oldroot vnode on top of the newroot vnode
  ve_shadow_mount(root_ve, moveref(oldroot_vn));

  ve_syncvn(newroot_ve);
}

void ve_add_child(ventry_t *parent, ventry_t *child) {
  ASSERT(!VE_ISMOUNT(parent));
  child = ve_getref(child); // add child ref to parent->children
  child->parent = ve_getref(parent);
  LIST_ADD(&parent->children, child, list);
  parent->chld_count++;
  if (VE_ISLINKED(child)) {
    VN(child)->parent_id = parent->id;
  }
}

void ve_remove_child(ventry_t *parent, ventry_t *child) {
  LIST_REMOVE(&parent->children, child, list);
  ve_putref(&child->parent);
  ve_putref(&child); // release parent->children ref
  parent->chld_count--;
}

ssize_t ve_get_path(ventry_t *ve, sbuf_t *buf) {
  ventry_t *root_ve = fs_root_getref();

  size_t pathlen = 0;
  ve = ve_getref(ve);
  if (ve == root_ve) {
    sbuf_write_char(buf, '/');
    pathlen += 1;
  }

  while (ve != root_ve) {
    size_t n;
    if (!str_eq_charp(ve->name, "/")) {
      n = sbuf_write_str_reverse(buf, ve->name);
      if (n == 0) goto nametoolong;
      pathlen += n;

      n = sbuf_write_char(buf, '/');
      if (n == 0) goto nametoolong;
      pathlen += 1;
    }

    ventry_t *parent = ve_getref(ve->parent);
    ve_putref_swap(&ve, &parent);
  }

  // reverse the path
  sbuf_reverse(buf);

  // write a null terminator
  sbuf_write_char(buf, 0);

  ve_putref(&ve);
  ve_putref(&root_ve);
  return (ssize_t) pathlen;

LABEL(nametoolong);
  ve_putref(&ve);
  ve_putref(&root_ve);
  return -ENAMETOOLONG; // name too long
}

bool ve_syncvn(ventry_t *ve) {
  if (!VE_ISLINKED(ve))
    return false;

  vnode_t *vn = VN(ve);
  ASSERT(ve->type == vn->type);

  // sync the state
  ve->state = vn->state;
  if (V_ISDEAD(ve) && V_ISDIR(ve)) {
    ASSERT(!VE_ISMOUNT(ve));
    // when a ventry enters the dead state we recursively sync all children and remove them.
    ASSERT(!VE_ISMOUNT(ve));
    ventry_t *child;
    while ((child = LIST_FIRST(&ve->children)) != NULL) {
      ve_syncvn(child);
      ve_remove_child(ve, child);
    }
    return false;
  } else if (!VE_ISMOUNT(ve) && V_ISALIVE(vn) && vn->vfs) {
    // we dont sync mounts because they are not part of the vnode's vfs
    ve->vfs_id = vn->vfs->id;
    ve->ops = vn->vfs->type->ve_ops;
  }
  return true;
}

void ve_hash(ventry_t *ve) {
  if (VE_OPS(ve)->v_hash)
    ve->hash = VE_OPS(ve)->v_hash(cstr_from_str(ve->name));
  else
    ve->hash = ve_hash_default(cstr_from_str(ve->name));
}

void _ve_cleanup(__move ventry_t **veref) {
  // called when last reference is released
  ventry_t *ve = moveref(*veref);
  ASSERT(ve != NULL);
  ASSERT(ve->state == V_DEAD);
  ASSERT(ve->chld_count == 0);
  ASSERT(ref_count(&ve->refcount) == 0);
  if (mtx_owner(&ve->lock) != NULL) {
    ASSERT(mtx_owner(&ve->lock) == curthread);
    mtx_unlock(&ve->lock);
  }

  DPRINTF("!!! ventry cleanup !!! {:+ve}\n", ve);
  if (VE_OPS(ve)->v_cleanup)
    VE_OPS(ve)->v_cleanup(ve);

  ve_putref(&ve->parent);
  vn_putref(&ve->vn);
  str_free(&ve->name);
  mtx_destroy(&ve->lock);
  kfree(ve);
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
