//
// Created by Aaron Gill-Braun on 2023-06-08.
//

#include <kernel/fs_utils.h>
#include <kernel/device.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#define FAIL(x, ...) panic("%s: " x, __func__, ##__VA_ARGS__)

void touch(const char *path) {
  int fd = fs_open(path, O_CREAT | O_WRONLY, 0777);
  if (fd < 0) {
    FAIL("{:s}: {:err}\n", path, fd);
  }

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("{:s}: close failed: {:err}\n", path, res);
  }
}

void mknod(const char *path, mode_t mode, dev_t dev) {
  int res = fs_mknod(path, mode, dev);
  if (res < 0) {
    FAIL("{:s}: {:err}\n", path, res);
  }
}

void mkdir(const char *path) {
  int res = fs_mkdir(path, 0777);
  if (res < 0) {
    FAIL("{:s}: {:err}\n", path, res);
  }
}

void stat(const char *path) {
  struct stat statbuf;
  int res = fs_stat(path, &statbuf);
  if (res < 0) {
    FAIL("{:s}: {:err}\n", path, res);
  }

  const char *type;
  switch (statbuf.st_mode & S_IFMT) {
    case S_IFREG: type = "file"; break;
    case S_IFDIR: type = "directory"; break;
    case S_IFCHR: type = "character device"; break;
    case S_IFBLK: type = "block device"; break;
    case S_IFIFO: type = "fifo"; break;
    case S_IFLNK: type = "symbolic link"; break;
    case S_IFSOCK: type = "socket"; break;
    default:
      type = "unknown";
      break;
  }

  kprintf("stat \"{:s}\":\n", path);
  kprintf("  type: {:$ <16s }  inode: {:d}\n", type, statbuf.st_ino);
  kprintf("  size: {:$ <16zu}  links: {:d}\n", statbuf.st_size, statbuf.st_nlink);
  kprintf("  device: {:d},{:d}\n", dev_major(statbuf.st_dev), dev_minor(statbuf.st_dev));
}

void ls(const char *path) {
  int fd = fs_opendir(path);
  if (fd < 0) {
    FAIL("{:s}: {:err}\n", path, fd);
  }

  kprintf("ls \"{:s}\":\n", path);

  ssize_t nread;
  char buf[512];
  while ((nread = fs_readdir(fd, buf, sizeof(buf))) > 0) {
    struct dirent *ent = (void *) buf;
    struct dirent *end = (void *) (buf + nread);
    while (ent < end) {
      kprintf(" {:- 4u} {:s}\n", ent->d_ino, ent->d_name);
      ent = offset_ptr(ent, ent->d_reclen);
    }
  }
  if (nread < 0) {
    FAIL("{:s}: readdir failed: {:err}\n", path, nread);
  }

  int res = fs_closedir(fd);
  if (res < 0) {
    FAIL("{:s}: closedir failed: {:err}\n", path, res);
  }
}

void cat(const char *path) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    FAIL("{:s}: {:err}\n", path, fd);
  }

  ssize_t nread;
  char buf[512];
  while ((nread = fs_read(fd, buf, sizeof(buf))) > 0) {
    kprintf("{:s}", buf);
  }
  if (nread < 0) {
    FAIL("{:s}: read failed: {:err}\n", path, nread);
  }

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("{:s}: close failed: {:err}\n", path, res);
  }
}

void echo(const char *path, const char *data) {
  int fd = fs_open(path, O_CREAT | O_WRONLY | O_APPEND, 0777);
  if (fd < 0) {
    FAIL("{:s}: {:err}\n", path, fd);
  }

  ssize_t nwrite = fs_write(fd, data, strlen(data));
  if (nwrite < 0) {
    FAIL("{:s}: write failed: {:err}\n", path, nwrite);
  }

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("{:s}: close failed: {:err}\n", path, res);
  }
}
