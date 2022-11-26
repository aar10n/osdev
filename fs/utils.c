//
// Created by Aaron Gill-Braun on 2021-01-24.
//

#include <utils.h>
#include <printf.h>
#include <thread.h>
#include <string.h>
#include <fs/device.h>

// These functions are for debugging/development purposes

static bool is_dot_or_dotdot(const char *name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

void fs_lsdir(const char *path) {
  int fd = fs_open(path, O_RDONLY | O_DIRECTORY, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return;
  }

  kprintf("listing directory \"%s\"\n", path);
  dentry_t *dentry;
  while ((dentry = fs_readdir(fd)) != NULL) {
    if (IS_IFDIR(dentry->mode) && !is_dot_or_dotdot(dentry->name)) {
      kprintf("  %s/\n", dentry->name);
    } else {
      kprintf("  %s\n", dentry->name);
    }
  }

  fs_close(fd);
}

void fs_readfile(const char *path) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return;
  }

  char *buf = kmalloc(128);
  ssize_t nbytes;
  while ((nbytes = fs_read(fd, buf, 128)) > 0) {
    buf[nbytes] = '\0';
    kprintf("%s", buf);
  }

  if (nbytes < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
  } else {
    kprintf("\n");
  }

  fs_close(fd);
}

void fs_writefile(const char *path, char *string) {
  int fd = fs_open(path, O_WRONLY | O_CREAT, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return;
  }

  size_t len = strlen(string);
  ssize_t nbytes = fs_write(fd, string, len);

  if (nbytes < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
  } else if (nbytes != len) {
    kprintf("error: failed to write all data\n");
  } else {
    kprintf("\n");
  }

  fs_close(fd);
}

blkdev_t *fs_get_blkdev(const char *path) {
  struct stat statbuf;
  if (fs_stat(path, &statbuf) < 0) {
    kprintf("error: failed to get blkdev: %s\n", path);
    kprintf("       %s\n", strerror(ERRNO));
    return NULL;
  }

  if (major(statbuf.st_dev) != DEVICE_BLKDEV) {
    kprintf("error: failed to get device: %s\n", path);
    kprintf("       not a block device\n");
    return NULL;
  }

  device_t *device = locate_device(statbuf.st_dev);
  if (device == NULL) {
    kprintf("error: failed to get device: %s\n", path);
    return NULL;
  }

  return device->device;
}
