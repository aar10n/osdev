//
// Created by Aaron Gill-Braun on 2021-07-26.
//

#include <devfs/devfs.h>
#include <ramfs/ramfs.h>
#include <drivers/serial.h>
#include <thread.h>
#include <event.h>
#include <panic.h>

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

// stdin
ssize_t devfs_stdin_read(file_t *file, char *buf, size_t count, off_t *offset) {
  off_t off = 0;
 wait_for_event:;
  key_event_t *event = wait_for_key_event();
  while (event != NULL && off < count) {
    char ch = key_event_to_character(event);
    if (ch != 0) {
      buf[off] = ch;
      off++;
    }
    event = event->next;
  }

  if (off == 0) {
    goto wait_for_event;
  }
  *offset = 0;
  return off;
}
ssize_t devfs_stdin_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return 0;
}

file_ops_t stdin_file_ops = {
  .read = devfs_stdin_read,
  .write = devfs_stdin_write,
};

// stdout
ssize_t devfs_stdout_read(file_t *file, char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return 0;
}
ssize_t devfs_stdout_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  serial_nwrite(COM1, buf, count);
  *offset = 0;
  return count;
}

file_ops_t stdout_file_ops = {
  .read = devfs_stdout_read,
  .write = devfs_stdout_write,
};

// stderr
ssize_t devfs_stderr_read(file_t *file, char *buf, size_t count, off_t *offset) {
  *offset = 0;
  return 0;
}
ssize_t devfs_stderr_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  serial_nwrite(COM1, buf, count);
  *offset = 0;
  return count;
}

file_ops_t stderr_file_ops = {
  .read = devfs_stderr_read,
  .write = devfs_stderr_write,
};

//

int devfs_post_mount(file_system_t *fs, super_block_t *sb) {
  //
  // special devices
  //


  // /dev/loop
  dev_t dev_loop = fs_register_blkdev(0, NULL);
  kassert(dev_loop != 0);
  if (fs_mknod("/dev/loop", S_IFBLK, dev_loop) < 0) {
    panic("failed to create /dev/loop");
  }

  // /dev/null
  chrdev_t *chrdev_null = chrdev_init(&null_file_ops);
  dev_t dev_null = fs_register_chrdev(0, chrdev_null);
  kassert(dev_null != 0);
  if (fs_mknod("/dev/null", S_IFCHR, dev_null) < 0) {
    panic("failed to create /dev/null");
  }

  // /dev/zero
  chrdev_t *chrdev_zero = chrdev_init(&zero_file_ops);
  dev_t dev_zero = fs_register_chrdev(0, chrdev_zero);
  kassert(dev_zero != 0);
  if (fs_mknod("/dev/zero", S_IFCHR, dev_zero) < 0) {
    panic("failed to create /dev/zero");
  }

  // /dev/stdin
  chrdev_t *chrdev_stdin = chrdev_init(&stdin_file_ops);
  dev_t dev_stdin = fs_register_chrdev(0, chrdev_stdin);
  kassert(dev_stdin != 0);
  if (fs_mknod("/dev/stdin", S_IFCHR, dev_stdin) < 0) {
    panic("failed to create /dev/stdin");
  }

  // /dev/stdout
  chrdev_t *chrdev_stdout = chrdev_init(&stdout_file_ops);
  dev_t dev_stdout = fs_register_chrdev(0, chrdev_stdout);
  kassert(dev_stdout != 0);
  if (fs_mknod("/dev/stdout", S_IFCHR, dev_stdout) < 0) {
    panic("failed to create /dev/stdout");
  }

  // /dev/stderr
  chrdev_t *chrdev_stderr = chrdev_init(&stderr_file_ops);
  dev_t dev_stderr = fs_register_chrdev(0, chrdev_stderr);
  kassert(dev_stderr != 0);
  if (fs_mknod("/dev/stderr", S_IFCHR, dev_stderr) < 0) {
    panic("failed to create /dev/stderr");
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
