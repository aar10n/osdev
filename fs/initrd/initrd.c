//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#include "initrd.h"

#include <fs/ramfs/ramfs.h>

#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

struct vfs_ops initrd_vfs_ops = {
  .v_mount = initrd_vfs_mount,
  .v_unmount = ramfs_vfs_unmount,
  .v_stat = initrd_vfs_stat,
  .v_cleanup = ramfs_vfs_cleanup,
};

struct vnode_ops initrd_vnode_ops = {
  .v_read = initrd_vn_read,
  .v_map = initrd_vn_map,
  .v_readlink = ramfs_vn_readlink,
  .v_readdir = ramfs_vn_readdir,
  .v_lookup = ramfs_vn_lookup,
  .v_cleanup = initrd_vn_cleanup,
};

struct ventry_ops initrd_ventry_ops = {
  .v_cleanup = ramfs_ve_cleanup,
};

static fs_type_t initrd_type = {
  .name = "initrd",
  .flags = VFS_RDONLY,
  .vfs_ops = &initrd_vfs_ops,
  .vnode_ops = &initrd_vnode_ops,
  .ventry_ops = &initrd_ventry_ops,
};


static void initrd_static_init() {
  if (fs_register_type(&initrd_type) < 0) {
    panic("failed to register initrd type\n");
  }
}
STATIC_INIT(initrd_static_init);
