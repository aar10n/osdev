//
// Created by Aaron Gill-Braun on 2021-01-24.
//

#include <fs/utils.h>
#include <fs.h>
#include <printf.h>
#include <mm/heap.h>

// These functions are for debugging/development purposes
// only. In the future, these will become proper commands
// running in userspace and will be removed.

void fs_lsdir(const char *path) {
  DIR *dirp = fs_opendir(path);
  dirent_t *dp;

  if (dirp == NULL) {
    kprintf("error: %s\n", strerror(errno));
    return;
  }

  kprintf("listing directory \"%s\"\n", path);
  while ((dp = fs_readdir(dirp)) != NULL) {
    kprintf("%s\n", dp->name);
  }

  fs_closedir(dirp);
  kprintf("\n");
}

void fs_readfile(const char *path) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(errno));
    return;
  }

  char *buf = kmalloc(128);
  ssize_t nbytes;
  while ((nbytes = fs_read(fd, buf, 128)) > 0) {
    kprintf("%s", buf);
  }

  if (nbytes < 0) {
    kprintf("error: %s\n", strerror(errno));
  } else {
    kprintf("\n");
  }

  fs_close(fd);
}

void fs_writefile(const char *path, char *string) {
  int fd = fs_open(path, O_WRONLY | O_CREAT, 0);
  if (fd < 0) {
    kprintf("error: %s\n", strerror(errno));
    return;
  }

  size_t len = strlen(string);
  ssize_t nbytes = fs_write(fd, string, len);

  if (nbytes < 0) {
    kprintf("error: %s\n", strerror(errno));
  } else if (nbytes != len) {
    kprintf("error: failed to write all data\n");
  } else {
    kprintf("\n");
  }

  fs_close(fd);
}
