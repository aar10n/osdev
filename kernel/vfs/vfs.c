//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/mm.h>
#include <kernel/device.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <rb_tree.h>

struct vtable {
  rb_tree_t *tree;
  size_t count;
  mtx_t lock; // spin mutex
};

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("vfs: %s: " fmt, __func__, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("vfs: %s: " fmt, __func__, ##__VA_ARGS__)

static id_t unique_vfs_id = 1;

static inline struct vtable *vtable_alloc() {
  struct vtable *table = kmallocz(sizeof(struct vtable));
  table->tree = create_rb_tree();
  return table;
}

static inline void vtable_free(struct vtable *table) {
  ASSERT(table->count == 0);
  rb_tree_free(table->tree);
  kfree(table);
}

//

__ref vfs_t *vfs_alloc(struct fs_type *type, int mount_flags) {
  vfs_t *vfs = kmallocz(sizeof(vfs_t));
  vfs->id = atomic_fetch_add(&unique_vfs_id, 1);
  vfs->state = V_EMPTY;
  vfs->mount_flags = type->flags | mount_flags; // inherit flags from fs type
  vfs->type = type;
  vfs->ops = type->vfs_ops;
  vfs->vtable = vtable_alloc();
  mtx_init(&vfs->lock, MTX_RECURSIVE, "vfs_lock");
  ref_init(&vfs->refcount);
  VN_DPRINTF("ref init id=%u<%p> [1]", vfs->id, vfs);
  DPRINTF("allocated vfs id=%u <%p>\n", vfs->id, vfs);
  return vfs;
}

void vfs_add_node(vfs_t *vfs, ventry_t *ve) {
  DPRINTF("adding {:+ve} to vfs id=%u\n", ve, vfs->id);
  ASSERT(VE_ISLINKED(ve));
  vnode_t *vn = VN(ve);
  ASSERT(vn->state == V_EMPTY);
  vn->state = V_ALIVE;
  vn->vfs = vfs_getref(vfs);
  if (vn->ops == NULL) {
    // allow filesystems to provide a per-vnode ops table
    vn->ops = vfs->type->vn_ops;
  }
  vn->device = vfs->device;

  struct vtable *table = vfs->vtable;
  if (rb_tree_find(table->tree, vn->id) != NULL)
    panic("vnode already exists in table {:vn}", vn);

  rb_tree_insert(table->tree, vn->id, vn_getref(vn));
  table->count++;
  LIST_ADD(&vfs->vnodes, vn, list);
  ve_syncvn(ve);
  DPRINTF("added {:+vn} to vfs id=%u [V_EMPTY -> V_ALIVE]\n", vn, vfs->id);
}

void vfs_remove_node(vfs_t *vfs, vnode_t *vn) {
  DPRINTF("removing {:+vn} from vfs=%u\n", vn, vfs->id);
  ASSERT(vn->state == V_ALIVE || vn->state == V_DEAD);
  vn->state = V_DEAD;
  // vn->vfs reference is released on vnode cleanup

  struct vtable *table = vfs->vtable;
  vnode_t *found = rb_tree_delete(table->tree, vn->id);
  ASSERT(found == vn);
  vn_putref(&found);
  table->count--;
  LIST_REMOVE(&vfs->vnodes, vn, list);
  DPRINTF("removed {:+vn} from vfs id=%u [V_ALIVE -> V_DEAD]\n", vn, vfs->id);
}

void _vfs_cleanup(__move vfs_t **vfsref) {
  vfs_t *vfs = moveref(*vfsref);
  ASSERT(vfs->state == V_DEAD);
  ASSERT(vfs->vtable->count == 0);
  ASSERT(vfs->root_ve == NULL);
  if (mtx_owner(&vfs->lock) != NULL) {
    ASSERT(mtx_owner(&vfs->lock) == curthread);
    mtx_unlock(&vfs->lock);
  }

  DPRINTF("!!! vfs cleanup !!! id=%u<%p>\n", vfs->id, vfs);
  if (VFS_OPS(vfs)->v_cleanup)
    VFS_OPS(vfs)->v_cleanup(vfs);

  vtable_free(vfs->vtable);
  mtx_destroy(&vfs->lock);
  kfree(vfs);
}

//

int vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve) {
  DPRINTF("mounting vfs id=%u at {:ve}\n", vfs->id, mount_ve);
  int res;
  if (!V_ISDIR(mount_ve)) {
    EPRINTF("mount point is not a directory\n");
    return -ENOTDIR;
  }
  if (VE_ISMOUNT(mount_ve)) {
    EPRINTF("mount point is already mounted\n");
    return -EBUSY;
  }
  if (mount_ve->chld_count > 0) {
    EPRINTF("mount point is not empty\n");
    return -ENOTEMPTY;
  }
  if (device && device->dtype != D_BLK) {
    EPRINTF("device is not a block device\n");
    return -ENOTBLK;
  }

  vfs_t *host_vfs = VN(mount_ve)->vfs;
  if (host_vfs && !vfs_lock(host_vfs))
    return -EINVAL; // vfs is dead

  // mount filesystem
  ventry_t *root_ve = NULL;
  if ((res = VFS_OPS(vfs)->v_mount(vfs, device, mount_ve, &root_ve)) < 0) {
    EPRINTF("failed to mount filesystem\n");
    vfs_unlock(host_vfs);
    return res;
  }
  assert_new_ventry_valid(root_ve);
  DPRINTF("mounted vfs id=%u at {:+ve} with root {:+ve}\n", vfs->id, mount_ve, root_ve);
  root_ve->parent = ve_getref(mount_ve); // allow us to traverse back to the mount point

  vnode_t *root_vn = VN(root_ve);
  root_vn->flags |= VN_ROOT;
  root_vn->parent_id = mount_ve->id;

  vfs->state = V_ALIVE;
  vfs->root_ve = moveref(root_ve);
  vfs->device = device;
  vfs_add_node(vfs, vfs->root_ve);

  ve_shadow_mount(mount_ve, vn_getref(root_vn));
  if (host_vfs)
    vfs_unlock(host_vfs);
  return 0;
}

int vfs_unmount(vfs_t *vfs, ventry_t *mount_ve) {
  // unmount process
  //   1. wait for all vfs readers and writers to finish
  //   2. unmount all submounts
  //   3. destroy all vnodes
  //   4. unmount filesystem
  //   5. unshadow mount point
  //   6. tear down the ventry tree
  //
  DPRINTF("unmounting vfs id=%u at {:ve}\n", vfs->id, mount_ve);
  int res;
  if (!V_ISDIR(mount_ve)) {
    EPRINTF("mount point is not a directory\n");
    return -ENOTDIR;
  }
  if (!VE_ISMOUNT(mount_ve)) {
    EPRINTF("mount point is not mounted\n");
    return -EINVAL;
  }

  // obtain exclusive access to vfs (vfs lock already held by caller)
  if (!vfs_begin_write_op(vfs))
    return -EINVAL;

  // set vfs to dead so that no new vnode operations can be started
  vfs->state = V_DEAD;
  // replace the root_ve parent reference to mount_ve with a self-reference
  ve_putref(&vfs->root_ve->parent);
  vfs->root_ve->parent = ve_getref(mount_ve);

  // unmount submounts
  LIST_FOR_IN(submount, &vfs->submounts, list) {
    if (!vfs_lock(submount))
      continue; // submount is dead

    ventry_t *submount_ve = ve_getref(submount->root_ve->parent);
    if (!ve_lock(submount_ve)) {
      ve_putref(&submount_ve);
      vfs_unlock(submount);
      continue; // mount point is dead
    }

    res = vfs_unmount(submount, submount_ve);
    vfs_unlock(submount);
    ve_putref(&submount_ve);
    if (res < 0)
      EPRINTF("failed to unmount submount: {:err} (continuing)\n", res);
  }

  // mark all vnodes as dead (but dont remove them yet)
  LIST_FOR_IN(vn, &vfs->vnodes, list) {
    if (vn_lock(vn)) {
      vn_begin_data_write(vn);
      vn_save(vn);
      vn->state = V_DEAD;  // mark as dead but keep vtable reference
      vn_end_data_write(vn);
      vn_unlock(vn);
    }
  }

  // now we can remove them from the filesystem
  vnode_t *vn;
  while ((vn = LIST_FIRST(&vfs->vnodes))) {
    vn = vn_getref(vn); // hold temp reference
    vfs_remove_node(vfs, vn);
    vn_putref(&vn);
  }

  // unmount filesystem
  if ((res = VFS_OPS(vfs)->v_unmount(vfs)) < 0) {
    EPRINTF("failed to unmount filesystem\n");
    return res;
  }

  // restore the shadowed mount vnode
  __ref vnode_t *root_vn = ve_unshadow_mount(mount_ve);

  // this sync causes the ventries to recursively mark themselves as dead
  // causing the ventry tree to be torn down and all sub-references released
  ve_syncvn(vfs->root_ve);
  ve_putref(&vfs->root_ve);

  vn_putref(&root_vn);
  DPRINTF("unmounted vfs id=%u\n", vfs->id);
  return 0;
}

int vfs_sync(vfs_t *vfs) {
  if (VFS_OPS(vfs)->v_sync)
    return VFS_OPS(vfs)->v_sync(vfs);
  return 0;
}

int vfs_stat(vfs_t *vfs, struct vfs_stat *stat) {
  if (VFS_OPS(vfs)->v_stat)
    return VFS_OPS(vfs)->v_stat(vfs, stat);
  return 0;
}
