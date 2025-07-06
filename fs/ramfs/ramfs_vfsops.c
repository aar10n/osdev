//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include "ramfs.h"

#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
// #define DPRINTF(fmt, ...) kprintf("ramfs_vfsops: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)

//

int ramfs_vfs_mount(vfs_t *vfs, device_t *device, ventry_t *mount_ve, __move ventry_t **rootve) {
  DPRINTF("mount vfs=%u\n", vfs->id);
  ramfs_mount_t *mount = ramfs_alloc_mount(vfs);
  vfs->data = mount;

  // create the root vnode
  vnode_t *vn = vn_alloc(1, &make_vattr(V_DIR, S_IFDIR));
  vn->data = mount->root;

  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);
  *rootve = moveref(ve);
  vn_putref(&vn);
  return 0;
}

int ramfs_vfs_unmount(vfs_t *vfs) {
  DPRINTF("unmount vfs=%u\n", vfs->id);
  // nothing to be done as freeing our data happens during cleanup
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

void ramfs_vfs_cleanup(vfs_t *vfs) {
  DPRINTF("cleanup\n");
  ramfs_mount_t *mount = vfs->data;
  // the nodes have already been free, clear the mount reference to root
  mount->root = NULL;
  ramfs_free_mount(mount);
}
