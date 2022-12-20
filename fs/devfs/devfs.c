//
// Created by Aaron Gill-Braun on 2021-07-26.
//

#include <devfs/devfs.h>
#include <ramfs/ramfs.h>
#include <drivers/serial.h>
#include <thread.h>
#include <panic.h>
#include <string.h>

// /dev/null
ssize_t devfs_null_read(file_t *file, char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return count;
}
ssize_t devfs_null_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return count;
}

file_ops_t null_file_ops = {
  .read = devfs_null_read,
  .write = devfs_null_write,
};

// /dev/zero
ssize_t devfs_zero_read(file_t *file, char *buf, size_t count, off_t *offset) {
  memset(buf, 0, count);
  *offset = 0;
  return count;
}
ssize_t devfs_zero_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return count;
}

file_ops_t zero_file_ops = {
  .read = devfs_zero_read,
  .write = devfs_zero_write,
};

//

int devfs_post_mount(file_system_t *fs, super_block_t *sb) {
  //
  // special devices
  //

  // /dev/loop
  dev_t dev_loop = fs_register_blkdev(0, NULL, NULL);
  kassert(dev_loop != 0);
  if (fs_mknod("/dev/loop", S_IFBLK, dev_loop) < 0) {
    panic("failed to create /dev/loop");
  }

  // /dev/null
  chrdev_t *chrdev_null = chrdev_init(&null_file_ops);
  dev_t dev_null = fs_register_chrdev(0, chrdev_null, NULL);
  kassert(dev_null != 0);
  if (fs_mknod("/dev/null", S_IFCHR, dev_null) < 0) {
    panic("failed to create /dev/null");
  }

  // /dev/zero
  chrdev_t *chrdev_zero = chrdev_init(&zero_file_ops);
  dev_t dev_zero = fs_register_chrdev(0, chrdev_zero, NULL);
  kassert(dev_zero != 0);
  if (fs_mknod("/dev/zero", S_IFCHR, dev_zero) < 0) {
    panic("failed to create /dev/zero");
  }

  return 0;
}

//

file_system_t devfs_file_system = {
  .name = "devfs",
  .flags = FS_NO_ROOT,
  .mount = ramfs_mount,
  .post_mount = devfs_post_mount,
};

void devfs_init() {
  devfs_file_system.sb_ops = ramfs_super_ops;
  devfs_file_system.inode_ops = ramfs_inode_ops;
  devfs_file_system.dentry_ops = ramfs_dentry_ops;
  devfs_file_system.file_ops = ramfs_file_ops;

  if (fs_register(&devfs_file_system) < 0) {
    panic("failed to register");
  }
}
