//
// Created by Aaron Gill-Braun on 2023-04-25.
//

#include <initrd/initrd.h>
#include <fs.h>
#include <panic.h>

static struct super_block_ops initrd_sb_ops = {
  .sb_mount = initrd_sb_mount,
  .sb_unmount = initrd_sb_unmount,
  .sb_read_inode = initrd_sb_read_inode,
};

static struct inode_ops initrd_i_ops = {
  .i_loaddir = initrd_i_loaddir,
};

static struct dentry_ops initrd_d_ops = {};

static struct file_ops initrd_f_ops = {};

static fs_type_t initrd_fs = {
  .name = "initrd",
  .flags = FS_RDONLY,
  .sb_ops = &initrd_sb_ops,
  .inode_ops = &initrd_i_ops,
  .dentry_ops = &initrd_d_ops,
  .file_ops = &initrd_f_ops,
};

//

static void initrd_register() {
  if (fs_register_type(&initrd_fs) < 0) {
    panic("failed to register initrd fs");
  }
}
MODULE_INIT(initrd_register);
