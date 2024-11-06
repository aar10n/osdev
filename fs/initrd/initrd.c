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

struct ventry_ops initrd_ventry_ops = {
  .v_cleanup = ramfs_ve_cleanup,
};

static fs_type_t initrd_type = {
  .name = "initrd",
  .vfs_ops = &initrd_vfs_ops,
  .ve_ops = &initrd_ventry_ops,
};


static void initrd_static_init() {
  // an initrd filesystem is just a ramfs filesystem that is pre-populated with
  // read-only files from the initial ramdisk image. all directories are still
  // writable, allowing for the creation of new files and directories. for that
  // reason we set the filesystem default vnode operations to `ramfs_vnode_ops`
  // and set the initrd node operations to `initrd_vnode_ops` during mount.
  initrd_type.vn_ops = &ramfs_vnode_ops;

  if (fs_register_type(&initrd_type) < 0) {
    panic("failed to register initrd type\n");
  }
}
STATIC_INIT(initrd_static_init);
