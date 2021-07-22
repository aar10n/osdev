//
// Created by Aaron Gill-Braun on 2021-01-24.
//

#include <utils.h>
#include <printf.h>
#include <thread.h>

// These functions are for debugging/development purposes
// only. In the future, these will become proper commands
// running in userspace and will be removed.

void fs_lsdir(const char *path) {
  int fd = fs_open(path, O_RDONLY | O_DIRECTORY, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return;
  }

  kprintf("[utils] listing directory \"%s\"\n", path);
  dentry_t *dentry;
  while ((dentry = fs_readdir(fd)) != NULL) {
    kprintf("%s\n", dentry->name);
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
