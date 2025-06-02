//
// Created by Aaron Gill-Braun on 2023-06-08.
//

#include <kernel/fs_utils.h>
#include <kernel/device.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#define FAIL(x, ...) panic("%s: " x, __func__, ##__VA_ARGS__)

void mount(const char *source, const char *mount, const char *fs_type, int flags) {
  kprintf(">>> mount(\"%s\", \"%s\", \"%s\", %d)\n", source, mount, fs_type, flags);
  int res = fs_mount(cstr_make(source), cstr_make(mount), fs_type, flags);
  kprintf(">>> mount -> {:err}\n", res);
  if (res < 0) {
    FAIL(" {:err} [source=%s, mount=%s]\n", res, source, mount);
  }
}

void replace_root(const char *new_root) {
  kprintf(">>> replace_root(\"%s\")\n", new_root);
  int res = fs_replace_root(cstr_make(new_root));
  kprintf(">>> replace_root -> {:err}\n", res);
  if (res < 0) {
    FAIL(" {:err} [new_root=%s]\n", res, new_root);
  }
}

void unmount(const char *path) {
  kprintf(">>> unmount(\"%s\")\n", path);
  int res = fs_unmount(cstr_make(path));
  kprintf(">>> unmount res={:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

int open(const char *path, mode_t mode) {
  kprintf(">>> open(\"%s\", %o)\n", path, mode);
  int fd = fs_open(cstr_make(path), O_CREAT | O_WRONLY, 0777);
  kprintf(">>> open -> {:err}\n", fd);
  if (fd < 0) {
    FAIL("%s: {:err}\n", path, fd);
  }
  return fd;
}

int dup(int fd) {
  kprintf(">>> dup(%d)\n", fd);
  int newfd = fs_dup(fd);
  kprintf(">>> dup -> {:err}\n", newfd);
  if (newfd < 0) {
    FAIL("%d: {:err}\n", fd, newfd);
  }
  return newfd;
}

void close(int fd) {
  kprintf(">>> close(%d)\n", fd);
  int res = fs_close(fd);
  kprintf(">>> close -> {:err}\n", res);
  if (res < 0) {
    FAIL("%d: close failed: {:err}\n", fd, res);
  }
}

void touch(const char *path) {
  kprintf(">>> touch(\"%s\")\n", path);
  int fd = fs_open(cstr_make(path), O_CREAT | O_WRONLY, 0777);
  kprintf(">>> touch -> {:err}\n", fd);
  if (fd < 0) {
    FAIL("%s: {:err}\n", path, fd);
  }

  kprintf(">>> close(%d)\n", fd);
  int res = fs_close(fd);
  kprintf(">>> close -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: close failed: {:err}\n", path, res);
  }
}

void mknod(const char *path, mode_t mode, dev_t dev) {
  kprintf(">>> mknod(\"%s\", %o, %u)\n", path, mode, dev);
  int res = fs_mknod(cstr_make(path), mode, dev);
  kprintf(">>> mknod -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

void unlink(const char *path) {
  kprintf(">>> unlink(\"%s\")\n", path);
  int res = fs_unlink(cstr_make(path));
  kprintf(">>> unlink -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

void mkdir(const char *path) {
  kprintf(">>> mkdir(\"%s\")\n", path);
  int res = fs_mkdir(cstr_make(path), 0777);
  kprintf(">>> mkdir -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

void rmdir(const char *path) {
  kprintf(">>> rmdir(\"%s\")\n", path);
  int res = fs_rmdir(cstr_make(path));
  kprintf(">>> rmdir -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

void chdir(const char *path) {
  kprintf(">>> chdir(\"%s\")\n", path);
  int res = fs_chdir(cstr_make(path));
  kprintf(">>> chdir -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
  }
}

void stat(const char *path) {
  kprintf(">>> stat(\"%s\")\n", path);
  struct stat statbuf;
  int res = fs_stat(cstr_make(path), &statbuf);
  kprintf(">>> stat -> {:err}\n", res);
  if (res < 0) {
    FAIL("%s: {:err}\n", path, res);
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

  kprintf("stat \"%s\":\n", path);
  kprintf("  type: {:$ <16s }  inode: %d\n", type, statbuf.st_ino);
  kprintf("  size: {:$ <16zu}  links: %d\n", statbuf.st_size, statbuf.st_nlink);
  kprintf("  device: %d,%d\n", dev_major(statbuf.st_dev), dev_minor(statbuf.st_dev));
}

void ls(const char *path) {
  kprintf(">>> ls(\"%s\")\n", path);
  int fd = fs_open(cstr_make(path), O_RDONLY|O_DIRECTORY, 0);
  if (fd < 0) {
    FAIL("%s: {:err}\n", path, fd);
  }

  kprintf("ls \"%s\":\n", path);

  ssize_t nread;
  char buf[512];
  while ((nread = fs_readdir(fd, buf, sizeof(buf))) > 0) {
    struct dirent *ent = (void *) buf;
    struct dirent *end = (void *) (buf + nread);
    while (ent < end) {
      kprintf(" {:- 4u} %s\n", ent->d_ino, ent->d_name);
      ent = offset_ptr(ent, ent->d_reclen);
    }
  }
  if (nread < 0) {
    FAIL("%s: readdir failed: {:err}\n", path, nread);
  }

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("%s: closedir failed: {:err}\n", path, res);
  }
}

void cat(const char *path) {
  kprintf(">>> cat(\"%s\")\n", path);
  int fd = fs_open(cstr_make(path), O_RDONLY, 0);
  if (fd < 0) {
    FAIL("%s: {:err}\n", path, fd);
  }

  ssize_t nread;
  char buf[512] = {0};
  while ((nread = fs_read(fd, buf, sizeof(buf))) > 0) {
    kprintf("{:.*s}", buf, nread);
  }
  if (nread < 0) {
    FAIL("%s: read failed: {:err}\n", path, nread);
  }
  kprintf("\n");

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("%s: close failed: {:err}\n", path, res);
  }
}

void echo(const char *path, const char *data) {
  kprintf(">>> echo(\"%s\", \"%s\")\n", path, data);
  int fd = fs_open(cstr_make(path), O_CREAT | O_WRONLY | O_APPEND, 0777);
  if (fd < 0) {
    FAIL("%s: {:err}\n", path, fd);
  }

  ssize_t nwrite = fs_write(fd, data, strlen(data));
  if (nwrite < 0) {
    FAIL("%s: write failed: {:err}\n", path, nwrite);
  }

  int res = fs_close(fd);
  if (res < 0) {
    FAIL("%s: close failed: {:err}\n", path, res);
  }
}
