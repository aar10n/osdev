//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <vfs/ventry.h>

#include <panic.h>
#include <printf.h>

#include "ramfs.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs_vfsops: %s: " fmt, __func__, ##__VA_ARGS__)
#define TRACE(str, ...) kprintf("ramfs_vfsops: " str, ##__VA_ARGS__)

struct vfs_ops ramfs_vfs_ops = {
  .v_mount = ramfs_vfs_mount,
  .v_unmount = ramfs_vfs_unmount,
  .v_sync = ramfs_vfs_sync,
  .v_stat = ramfs_vfs_stat,
};


int ramfs_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **rootve) {
  TRACE("mount\n");
  struct vattr attr = {.type = V_DIR, .mode = S_IFDIR};

  ramfs_mount_t *mount = kmallocz(sizeof(ramfs_mount_t));
  mount->vfs = vfs;
  vfs->data = mount;

  // create root node
  ramfs_node_t *node = ramfs_node_alloc(mount, attr.type, attr.mode);

  // create root vnode
  vnode_t *vn = vn_alloc(0, &attr);
  vn->data = node;
  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);

  *rootve = ve_moveref(&ve);
  return 0;
}

int ramfs_vfs_unmount(vfs_t *vfs) {
  TRACE("unmount\n");
  // all the node will all have been freed before this point
  kfree(vfs->data);
  vfs->data = NULL;
  return 0;
}

int ramfs_vfs_sync(vfs_t *vfs) {
  return 0;
}

int ramfs_vfs_stat(vfs_t *vm, struct vfs_stat *stat) {
  ramfs_mount_t *mount = vm->data;
  // TODO: track mount memory usage
  stat->total_files = mount->num_nodes;
  return 0;
}

