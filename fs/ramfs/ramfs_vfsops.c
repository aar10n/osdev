//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include "ramfs.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs_vfsops: %s: " fmt, __func__, ##__VA_ARGS__)
#define TRACE(str, ...) kprintf("ramfs_vfsops: " str, ##__VA_ARGS__)

//

int ramfs_vfs_mount(vfs_t *vfs, device_t *device, __move ventry_t **rootve) {
  TRACE("mount\n");

  ramfs_mount_t *mount = ramfs_alloc_mount(vfs);
  vfs->data = mount;

  // create root vnode
  vnode_t *vn = vn_alloc(0, &make_vattr(V_DIR, S_IFDIR));
  vn->data = mount->root;
  ventry_t *ve = ve_alloc_linked(cstr_new("/", 1), vn);

  *rootve = ve_moveref(&ve);
  return 0;
}

int ramfs_vfs_unmount(vfs_t *vfs) {
  TRACE("unmount\n");

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

