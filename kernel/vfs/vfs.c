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

#include <atomic.h>
#include <rb_tree.h>

struct vtable {
  rb_tree_t *tree;
  size_t count;
  spinlock_t lock;
};

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("vfs: %s: " fmt, __func__, ##__VA_ARGS__)

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

static void vfs_cleanup(vfs_t *vfs) {
  ASSERT(vfs->state == V_DEAD);
  ASSERT(vfs->vtable->count == 0);
  vtable_free(vfs->vtable);
  kfree(vfs);
}

//

vfs_t *vfs_alloc(struct fs_type *type, int mount_flags) __move {
  vfs_t *vfs = kmallocz(sizeof(vfs_t));
  vfs->id = atomic_fetch_add(&unique_vfs_id, 1);
  vfs->state = V_EMPTY;
  vfs->mount_flags = type->flags | mount_flags; // inherit flags from fs type
  vfs->type = type;
  vfs->ops = type->vfs_ops;
  vfs->vtable = vtable_alloc();
  mutex_init(&vfs->lock, MUTEX_REENTRANT);
  ref_init(&vfs->refcount);
  return vfs;
}

void vfs_release(__move vfs_t **ref) {
  if (*ref) {
    if (ref_put(&(*ref)->refcount, NULL)) {
      vfs_cleanup(*ref);
      *ref = NULL;
    }
  }
}

void vfs_add_node(vfs_t *vfs, ventry_t *ve) {
  ASSERT(VE_ISLINKED(ve));
  vnode_t *vn = VN(ve);
  ASSERT(vn->state == V_EMPTY);
  vn->state = V_ALIVE;
  vn->vfs = vfs_getref(vfs);
  vn->ops = vfs->type->vnode_ops;
  vn->device = vfs->device;

  struct vtable *table = vfs->vtable;
  if (rb_tree_find(table->tree, vn->id) != NULL)
    panic("vnode already exists in table [id=%u]", vn->id);

  rb_tree_insert(table->tree, vn->id, vn_getref(vn));
  table->count++;
  LIST_ADD(&vfs->vnodes, vn, list);
  ve_syncvn(ve);
}

void vfs_remove_node(vfs_t *vfs, vnode_t *vn) {
  ASSERT(vn->state == V_ALIVE);
  vn->state = V_DEAD;

  struct vtable *table = vfs->vtable;
  vnode_t *found = rb_tree_delete(table->tree, vn->id);
  ASSERT(found == vn);
  vn_release(&found);
  // release vnode vfs reference on cleanup
  table->count--;
  LIST_REMOVE(&vfs->vnodes, vn, list);
}

//

int vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve) {
  int res;
  if (!V_ISDIR(mount_ve)) {
    DPRINTF("mount point is not a directory\n");
    return -ENOTDIR;
  }
  if (VE_ISMOUNT(mount_ve)) {
    DPRINTF("mount point is already mounted\n");
    return -EBUSY;
  }
  if (mount_ve->chld_count > 0) {
    DPRINTF("mount point is not empty\n");
    return -ENOTEMPTY;
  }
  if (device && device->dtype != D_BLK) {
    DPRINTF("device is not a block device\n");
    return -ENOTBLK;
  }

  vfs_t *host_vfs = VN(mount_ve)->vfs;
  if (host_vfs && !vfs_lock(host_vfs))
    return -EINVAL; // vfs is dead

  // mount filesystem
  ventry_t *root_ve = NULL;
  if ((res = VFS_OPS(vfs)->v_mount(vfs, device, &root_ve)) < 0) {
    DPRINTF("failed to mount filesystem\n");
    // if host_vfs
    vfs_unlock(host_vfs);
    return res;
  }
  assert_new_ventry_valid(root_ve);
  root_ve->parent = ve_getref(mount_ve); // allow us to traverse back to the mount point

  vnode_t *root_vn = VN(root_ve);
  root_vn->flags |= VN_ROOT;
  root_vn->parent_id = mount_ve->id;

  vfs->state = V_ALIVE;
  vfs->root_ve = ve_moveref(&root_ve);
  vfs->device = device;
  vfs_add_node(vfs, vfs->root_ve);

  ve_shadow_mount(mount_ve, vfs->root_ve);
  if (host_vfs)
    vfs_unlock(host_vfs);
  return 0;
}

int vfs_unmount(vfs_t *vfs, ventry_t *mount) {
  int res;
  if (!V_ISDIR(mount)) {
    DPRINTF("mount point is not a directory\n");
    return -ENOTDIR;
  }
  if (!VE_ISMOUNT(mount)) {
    DPRINTF("mount point is not mounted\n");
    return -EINVAL;
  }

  // unmount process
  //   1. wait for all vfs readers and writers to finish
  //   2. unmount all submounts
  //   3. destroy all vnodes
  //   4. unmount filesystem
  //   5. unshadow mount point

  // obtain exclusive access to vfs (vfs lock already held by caller)
  if (!vfs_begin_write_op(vfs)) {
    return -EINVAL;
  }

  // set vfs to dead so that no new vnode operations can be started
  vfs->state = V_DEAD;

  // unmount submounts
  LIST_FOR_IN(submount, &vfs->submounts, list) {
    if (!vfs_lock(submount))
      continue; // submount is dead

    ventry_t *mount_ve = ve_getref(submount->root_ve->parent);
    if (!ve_lock(mount_ve)) {
      ve_release(&mount_ve);
      vfs_unlock(submount);
      continue; // mount point is dead
    }

    res = vfs_unmount(submount, mount_ve);
    vfs_unlock(submount);
    ve_release(&mount_ve);
    if (res < 0) {
      DPRINTF("failed to unmount submount: {:err} (continuing)\n", res);
    }
  }

  // destroy all vnodes
  vnode_t *vn = vn_getref(LIST_FIRST(&vfs->vnodes));
  while (vn) {
    vnode_t *next = vn_getref(LIST_NEXT(vn, list));

    // grab the vnode lock and write lock to ensure exclusive access
    if (!vn_lock(vn) || !vn_begin_data_write(vn)) {
      vn = next;
      continue;
    }

    vn_save(vn);
    vfs_remove_node(vfs, vn);

    vn_end_data_write(vn);
    vn_unlock(vn);
    vn_release(&vn);
    vn = vn_moveref(&next);
  }

  // unmount
  if ((res = VFS_OPS(vfs)->v_unmount(vfs)) < 0) {
    DPRINTF("failed to unmount filesystem\n");
    return res;
  }

  ventry_t *root_ve = ve_moveref(&vfs->root_ve);
  ve_release(&root_ve->parent);
  ve_release(&root_ve);
  ve_unshadow_mount(mount);
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

