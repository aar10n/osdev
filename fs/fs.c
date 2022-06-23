//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fs.h>
#include <dcache.h>
#include <dentry.h>
#include <device.h>
#include <file.h>
#include <inode.h>
#include <path.h>
#include <thread.h>
#include <process.h>
#include <hash_table.h>
#include <panic.h>

#include <ramfs/ramfs.h>
#include <devfs/devfs.h>
#include <ext2/ext2.h>
#include <printf.h>
#include <cpu/cpu.h>

// #define FS_DEBUG
#ifdef FS_DEBUG
#define fs_trace_debug(str, args...) kprintf("[fs] " str "\n", ##args)
#else
#define fs_trace_debug(str, args...)
#endif

dentry_t *fs_root;
map_t(file_system_t *) fs_types;


int validate_open_flags(dentry_t *dentry, int flags) {
  uint32_t type = flags & OPEN_TYPE_MASK;
  if (type & (type - 1)) {
    // if more than one type flag is set
    ERRNO = EINVAL;
    return -1;
  }

  if (dentry == NULL) {
    if (!(flags & O_CREAT)) {
      if (ERRNO == ENOTDIR) {
        ERRNO = ENOTDIR;
        return -1;
      }
      ERRNO = ENOENT;
      return -1;
    }
  } else {
    if ((flags & O_CREAT && flags & O_EXCL)) {
      ERRNO = EEXIST;
      return -1;
    }
    if (IS_IFDIR(dentry->mode) && (flags & O_EXEC)) {
      ERRNO = EINVAL;
      return -1;
    }
    if (IS_IFDIR(dentry->mode) && (flags & O_WRONLY || flags & O_RDWR)) {
      ERRNO = EISDIR;
      return -1;
    }
    if (IS_IFDIR(dentry->mode) && (flags & O_CREAT && !(flags & O_DIRECTORY))) {
      ERRNO = EISDIR;
      return -1;
    }
  }
  return 0;
}

//

dentry_t *dentry_from_basename(dentry_t *parent, const char *path) {
  path_t p = str_to_path(path);
  if (p_len(p) >= MAX_PATH) {
    ERRNO = ENAMETOOLONG;
    return NULL;
  }

  path_t base = path_basename(p);
  if (p_len(base) >= MAX_FILE_NAME) {
    ERRNO = ENAMETOOLONG;
    return NULL;
  }

  char name[MAX_FILE_NAME + 1];
  pathcpy(name, base);
  return d_alloc(parent, name);
}

int mount_root(device_t *device, file_system_t *fs) {
  kassert(!(fs->flags & FS_NO_ROOT));

  dentry_t *old_root = fs_root;
  dentry_t *dentry = d_alloc(NULL, "/");
  dentry->parent = dentry;

  dev_t devid = device ? device->dev : 0;
  blkdev_t *blkdev = device ? device->device : NULL;
  super_block_t *sb = fs->mount(fs, devid, blkdev, dentry);
  if (sb == NULL) {
    return -1;
  }

  dentry->mode |= S_ISLDD;
  sb->fs = fs;
  sb->dev = blkdev;
  sb->ops = fs->sb_ops;
  sb->root = dentry;

  if (fs->post_mount && fs->post_mount(fs, sb) < 0) {
    return -1;
  }

  //

  current_thread->preempt_count++;
  dentry_t *child = LIST_FIRST(&old_root->children);
  while (child) {
    if (strcmp(child->name, ".") == 0 || strcmp(child->name, "..") == 0) {
      child = LIST_NEXT(child, siblings);
      continue;
    }

    if (IS_IFMNT(child->mode)) {
      LIST_REMOVE(&old_root->children, child, siblings);
      LIST_ADD(&dentry->children, child, siblings);

      dentry_t *cchild = NULL;
      LIST_FOREACH(cchild, &child->children, siblings) {
        if (strcmp(cchild->name, "..") == 0) {
          d_detach(cchild);
          d_attach(cchild, dentry->inode);
          break;
        }
      }
    }

    child = LIST_NEXT(child, siblings);
  }

  fs_root = dentry;
  current_thread->preempt_count--;
  return 0;
}

int mount_internal(const char *path, device_t *device, file_system_t *fs) {
  if (device && major(device->dev) != DEVICE_BLKDEV) {
    ERRNO = ENOTBLK;
    return -1;
  }

  dentry_t *parent = NULL;
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, &parent);
  if (dentry == fs_root) {
    return mount_root(device, fs);
  } else if (dentry != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  // mount filesystem
  dentry = dentry_from_basename(parent, path);
  if (dentry == NULL) {
    return -1;
  }

  if (fs->flags & FS_NO_ROOT) {
    if (i_mkdir(parent->inode, dentry, S_IFMNT | S_IFDIR) < 0) {
      d_destroy(dentry);
      return -1;
    }
  }

  dev_t devid = device ? device->dev : 0;
  blkdev_t *blkdev = device ? device->device : NULL;
  super_block_t *sb = fs->mount(fs, devid, blkdev, dentry);
  if (sb == NULL) {
    return -1;
  } else if (!(fs->flags & FS_NO_ROOT)) {
    d_add_child(parent, dentry);
  }
  dentry->mode |= S_IFMNT | S_ISLDD;
  sb->fs = fs;
  sb->dev = blkdev;
  sb->ops = fs->sb_ops;
  sb->root = dentry;

  if (fs->post_mount) {
    return fs->post_mount(fs, sb);
  }

  return 0;
}


//
// Filesystem API
//


void fs_init() {
  dcache_init();
  path_init();
  map_init(&fs_types);

  ramfs_init();
  devfs_init();
  ext2_init();

  file_system_t *ramfs = map_get(&fs_types, "ramfs");
  kassert(ramfs != NULL);

  dentry_t *dentry = d_alloc(NULL, "/");
  dentry->parent = dentry;
  if (ramfs->mount(ramfs, 0, NULL, dentry) < 0) {
    panic("failed to mount root fs");
  }
  d_populate_dir(dentry);
  fs_root = dentry;

  // mount device filesystem
  file_system_t *devfs = map_get(&fs_types, "devfs");
  kassert(devfs != NULL);

  if (mount_internal("/dev", NULL, devfs) < 0) {
    panic("failed to mount /dev");
  }

  // initialize framebuf
  framebuf_init();
}

int fs_register(file_system_t *fs) {
  if (map_get(&fs_types, fs->name)) {
    ERRNO = EINVAL;
    return -1;
  }
  map_set(&fs_types, (char *) fs->name, fs);
  return 0;
}

dev_t fs_register_blkdev(uint8_t minor, blkdev_t *blkdev, device_ops_t *ops) {
  return register_blkdev(minor, blkdev, ops);
}

dev_t fs_register_chrdev(uint8_t minor, chrdev_t *chrdev, device_ops_t *ops) {
  return register_chrdev(minor, chrdev, ops);
}

dev_t fs_register_framebuf(uint8_t minor, framebuf_t *framebuf, device_ops_t *ops) {
  return register_framebuf(minor, framebuf, ops);
}

//

int fs_mount(const char *path, const char *device, const char *format) {
  dentry_t *devnode = resolve_path(device, *current_process->pwd, 0, NULL);
  if (devnode == NULL) {
    ERRNO = ENODEV;
    return -1;
  } else if (!IS_IFBLK(devnode->mode)) {
    ERRNO = ENOTBLK;
    return -1;
  }

  device_t *dev = locate_device(devnode->inode->dev);
  if (dev == NULL) {
    ERRNO = ENODEV;
    return -1;
  }

  file_system_t *fs = map_get(&fs_types, (char *) format);
  return mount_internal(path, dev, fs);
}

int fs_unmount(const char *path) {
  ERRNO = ENOTSUP;
  return -1;
}

//

int fs_open(const char *path, int flags, mode_t mode) {
  mode = (mode & I_PERM_MASK) | (flags & O_DIRECTORY ? S_IFDIR : S_IFREG);

  dentry_t *parent = NULL;
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, &parent);
  if (dentry == NULL && parent == NULL) {
    return -1;
  }

  int result = validate_open_flags(dentry, flags);
  if (result < 0) {
    return -1;
  }

  if (dentry == NULL && flags & O_CREAT) {
    dentry = dentry_from_basename(parent, path);
    if (dentry == NULL) {
      return -1;
    }

    if (flags & O_DIRECTORY) {
      result = i_mkdir(parent->inode, dentry, mode);
    } else {
      result = i_create(parent->inode, dentry, mode);
    }

    if (result < 0) {
      d_destroy(dentry);
      return -1;
    }
  } else if (dentry != NULL && flags & O_TRUNC) {
    i_truncate(dentry->inode);
  }

  file_t *file = f_alloc(dentry, flags);
  if (file == NULL) {
    return -1;
  }

  result = f_open(file, dentry);
  if (result < 0) {
    return -1;
  }
  return file->fd;
}

int fs_creat(const char *path, mode_t mode) {
  return fs_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int fs_mkdir(const char *path, mode_t mode) {
  dentry_t *parent = NULL;
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, &parent);
  if (dentry != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  dentry = dentry_from_basename(parent, path);
  if (dentry == NULL) {
    return -1;
  }

  int result = i_mkdir(parent->inode, dentry, mode);
  if (result < 0) {
    d_destroy(dentry);
    return -1;
  }
  return 0;
}

int fs_mknod(const char *path, mode_t mode, dev_t dev) {
  mode_t type = mode & I_TYPE_MASK;
  if ((type & (type - 1)) || (type & ~I_MKNOD_MASK)) {
    // if more than one type flag is set or an incorrect
    // type was specified
    ERRNO = EINVAL;
    return -1;
  }

  dentry_t *parent = NULL;
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, &parent);
  if (dentry != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  dentry = dentry_from_basename(parent, path);
  if (dentry == NULL) {
    return -1;
  }

  int result = i_mknod(parent->inode, dentry, mode, dev);
  if (result < 0) {
    d_destroy(dentry);
    return -1;
  }
  return 0;
}

int fs_close(int fd) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  if (f_flush(file) < 0) {
    return -1;
  }

  f_release(file);
  return 0;
}

//

int fs_stat(const char *path, stat_t *statbuf) {
  if (statbuf == NULL) {
    ERRNO = ENOBUFS;
    return -1;
  }

  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, NULL);
  if (dentry == NULL) {
    return -1;
  }

  inode_t *inode = dentry->inode;
  statbuf->st_dev = inode->dev;
  statbuf->st_ino = inode->ino;
  statbuf->st_mode = inode->mode;
  statbuf->st_nlink = inode->nlink;
  statbuf->st_uid = inode->uid;
  statbuf->st_gid = inode->gid;
  statbuf->st_rdev = 0;
  statbuf->st_size = inode->size;
  statbuf->st_atim = (struct timespec) { .tv_sec = inode->atime };
  statbuf->st_mtim = (struct timespec) { .tv_sec = inode->mtime };
  statbuf->st_ctim = (struct timespec) { .tv_sec = inode->ctime };
  statbuf->st_blksize = inode->blksize;
  statbuf->st_blocks = inode->blocks;
  return 0;
}

int fs_fstat(int fd, stat_t *statbuf) {
  if (statbuf == NULL) {
    ERRNO = ENOBUFS;
    return -1;
  }

  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  inode_t *inode = file->dentry->inode;
  statbuf->st_dev = inode->dev;
  statbuf->st_ino = inode->ino;
  statbuf->st_mode = inode->mode;
  statbuf->st_nlink = inode->nlink;
  statbuf->st_uid = inode->uid;
  statbuf->st_gid = inode->gid;
  statbuf->st_rdev = 0;
  statbuf->st_size = inode->size;
  statbuf->st_atim = (struct timespec) { .tv_sec = inode->atime };
  statbuf->st_mtim = (struct timespec) { .tv_sec = inode->mtime };
  statbuf->st_ctim = (struct timespec) { .tv_sec = inode->ctime };
  statbuf->st_blksize = inode->blksize;
  statbuf->st_blocks = inode->blocks;
  return 0;
}

//

ssize_t fs_read(int fd, void *buf, size_t nbytes) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }
  return f_read(file, buf, nbytes);
}

ssize_t fs_write(int fd, void *buf, size_t nbytes) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  if (file->flags & O_APPEND) {
    file->pos = file->dentry->inode->size;
  }
  return f_write(file, buf, nbytes);
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }
  return f_lseek(file, offset, whence);
}

//

int fs_dup(int fd) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  file_t *dup = f_dup(file, -1);
  if (dup == NULL) {
    return -1;
  }
  return dup->fd;
}

int fs_dup2(int fd, int fd2) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  file_t *file2 = f_locate(fd2);
  if (file2 != NULL) {
    int result = fs_close(fd2);
    if (result < 0) {
      return -1;
    }
  }

  file_t *dup = f_dup(file, fd2);
  if (dup == NULL) {
    return -1;
  }
  return fd2;
}

int fs_fcntl(int fd, int cmd, uint64_t arg) {
  file_t *file = f_locate(fd);
  if (file == NULL) {
    ERRNO = EBADF;
    return -1;
  }

  if (cmd == F_DUPFD) {
    // duplicate file descriptor
  } else if (cmd == F_DUPFD_CLOEXEC) {
    // duplicate file descriptor and set close-on-exec flag
  } else if (cmd == F_GETFD) {
    // get file descriptor flags
    return file->fd_flags;
  } else if (cmd == F_SETFD) {
    // set file descriptor flags
    file->fd_flags = (int) arg;
    return 0;
  } else if (cmd == F_GETFL) {
    // get file status flags
  } else if (cmd == F_SETFL) {
    // set file status flags
  } else if (cmd == F_SETLK) {
    // aquire or release file lock
  } else if (cmd == F_SETLKW) {
    // same as F_SETLK but wait for conflicting
    // lock to be freed
  } else if (cmd == F_GETLK) {
    // checks for a conflicting file lock
  } else if (cmd == F_GETOWN) {
    // get process that is receiving SIGIO and SIGURG
    // signals for events on the file
  } else if (cmd == F_SETOWN) {
    // set process that will receive SIGIO and SIGURG
    // signals for events on the file
  }

  ERRNO = ENOTSUP;
  return -1;
}

//

dentry_t *fs_readdir(int fd) {
  file_t *dir = f_locate(fd);
  if (dir == NULL) {
    ERRNO = EBADF;
    return NULL;
  } else if (!IS_IFDIR(dir->mode)) {
    ERRNO = ENOTDIR;
    return NULL;
  }
  return f_readdir(dir);
}

long fs_telldir(int fd) {
  file_t *dir = f_locate(fd);
  if (dir == NULL || !IS_IFDIR(dir->mode)) {
    ERRNO = EBADF;
    return -1;
  }
  return dir->pos;
}

void fs_seekdir(int fd, long loc) {
  file_t *dir = f_locate(fd);
  if (dir == NULL || !IS_IFDIR(dir->mode) || loc < 0 || loc == dir->pos) {
    ERRNO = EBADF;
    return;
  }

  dentry_t *dentry;
  if (dir->pos == 0) {
    dentry = LIST_FIRST(&dir->dentry->children);
  } else {
    dentry = dir->dentry;
  }

  long pos = dir->pos;
  while (dentry && pos != loc) {
    if (pos > loc) {
      pos--;
      dentry = LIST_PREV(dentry, siblings);
    } else {
      pos++;
      dentry = LIST_NEXT(dentry, siblings);
    }
  }

  if (dentry == NULL) {
    return;
  }

  if (pos == 0) {
    dir->dentry = dentry->parent;
  } else {
    dir->dentry = LIST_PREV(dentry, siblings);
  }
  dir->pos = pos;
}

void fs_rewinddir(int fd) {
  file_t *dir = f_locate(fd);
  if (dir == NULL || !IS_IFDIR(dir->mode) || dir->pos == 0) {
    return;
  }

  dir->dentry = dir->dentry->parent;
  dir->pos = 0;
}

//

int fs_link(const char *path1, const char *path2) {
  dentry_t *a = resolve_path(path1, *current_process->pwd, 0, NULL);
  if (a == NULL) {
    return -1;
  }

  dentry_t *parent = NULL;
  dentry_t *b = resolve_path(path2, *current_process->pwd, 0, &parent);
  if (b != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  inode_t *inode = a->inode;
  if (inode->blkdev != parent->inode->blkdev) {
    ERRNO = EXDEV;
    return -1;
  } else if (IS_IFDIR(inode->mode)) {
    ERRNO = EISDIR;
    return -1;
  }

  dentry_t *dentry = dentry_from_basename(parent, path2);
  if (dentry == NULL) {
    return -1;
  }
  return i_link(parent->inode, a, dentry);
}

int fs_unlink(const char *path) {
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, NULL);
  if (dentry == NULL) {
    return -1;
  }
  return i_unlink(dentry->parent->inode, dentry);
}

int fs_symlink(const char *path1, const char *path2) {
  char path[MAX_PATH + 1];
  int result = expand_path(path1, *current_process->pwd, path, MAX_PATH + 1);
  if (result < 0) {
    return -1;
  }

  dentry_t *parent = NULL;
  dentry_t *dentry = resolve_path(path2, *current_process->pwd, 0, &parent);
  if (dentry != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  dentry = dentry_from_basename(parent, path2);
  if (dentry == NULL) {
    return -1;
  }

  result = i_symlink(parent->inode, dentry, path);
  if (result < 0) {
    d_destroy(dentry);
    return -1;
  }
  return 0;
}

int fs_rename(const char *oldfile, const char *newfile) {
  dentry_t *oldf = resolve_path(oldfile, *current_process->pwd, 0, NULL);
  if (oldf == NULL) {
    return -1;
  }

  dentry_t *parent;
  dentry_t *dentry = resolve_path(oldfile, *current_process->pwd, 0, &parent);
  if (dentry != NULL) {
    ERRNO = EEXIST;
    return -1;
  } else if (parent == NULL) {
    return -1;
  }

  dentry = dentry_from_basename(parent, newfile);
  if (dentry == NULL) {
    return -1;
  }

  int result = i_rename(oldf->parent->inode, oldf, parent->inode, dentry);
  if (result < 0) {
    d_destroy(dentry);
    return -1;
  }
  return 0;
}

ssize_t fs_readlink(const char *restrict path, char *restrict buf, size_t bufsize) {
  ERRNO = ENOTSUP;
  return -1;
}

int fs_rmdir(const char *path) {
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, NULL);
  if (dentry == NULL) {
    return -1;
  }

  if (!IS_IFDIR(dentry->mode)) {
    ERRNO = ENOTDIR;
    return -1;
  }
  // check empty

  return i_rmdir(dentry->parent->inode, dentry);
}

int fs_chdir(const char *path) {
  dentry_t *dentry = resolve_path(path, *current_process->pwd, 0, NULL);
  if (dentry == NULL) {
    return -1;
  }
  current_process->pwd = &dentry;
  return 0;
}

int fs_chmod(const char *path, mode_t mode) {
  ERRNO = ENOTSUP;
  return -1;
}

int fs_chown(const char *path, uid_t owner, gid_t group) {
  ERRNO = ENOTSUP;
  return -1;
}

char *fs_getcwd(char *buf, size_t size) {
  if (buf == NULL || size == 0) {
    ERRNO = EINVAL;
    return NULL;
  }

  int result = get_dentry_path(*current_process->pwd, buf, size, NULL);
  if (result < 0) {
    ERRNO = ERANGE;
    return NULL;
  }
  return buf;
}

//

void *fs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  file_t *file = NULL;
  if (fd == -1) {
    if (!(flags & MAP_ANONYMOUS)) {
      ERRNO = EINVAL;
      return MAP_FAILED;
    }
  } else {
    file = f_locate(fd);
    if (file == NULL) {
      ERRNO = EBADF;
      return MAP_FAILED;
    }

    if (prot & PROT_WRITE && file->flags & O_RDONLY) {
      ERRNO = EACCES;
      return MAP_FAILED;
    }
    if (prot & PROT_EXEC && !(file->flags & O_EXEC)) {
      ERRNO = EACCES;
      return MAP_FAILED;
    }
  }

  unreachable;
  // uintptr_t va = (uintptr_t) addr;
  // if (!vm_find_free_area(flags & MAP_FIXED ? EXACTLY : ABOVE, &va, len)) {
  //   ERRNO = ENOMEM;
  //   return MAP_FAILED;
  // }
  // vm_mark_reserved(va, len);
  //
  // uint16_t pflags = PE_USER;
  // if (prot & PROT_WRITE) {
  //   pflags |= PE_WRITE;
  // }
  // if (prot & PROT_EXEC) {
  //   pflags |= PE_EXEC;
  // }
  //
  // if (fd == -1) {
  //   page_t *pages = alloc_frames(SIZE_TO_PAGES(len), pflags);
  //   vm_map_page_vaddr(va, pages);
  //   vm_update_attributes(va, AREA_PAGE | AREA_MMAP, pages);
  //   // zero the allocated pages
  //   uint64_t rflags = cli_save();
  //   uint64_t cr0 = read_cr0();
  //   write_cr0(cr0 & ~(1 << 16)); // disable cr0.WP
  //   memset((void *) va, 0, len);
  //   write_cr0(cr0); // re-enable cr0.WP
  //   sti_restore(rflags);
  // } else if (f_mmap(file, va, len, pflags) < 0) {
  //   // TODO: unmark as reserved
  //   return NULL;
  // }
  //
  // if (fd >= 0) {
  //   vm_attach_file(va, file);
  //   vm_update_attributes(va, AREA_FILE | AREA_MMAP, file);
  // }
  // return (void *) va;
}

int fs_munmap(void *addr, size_t len) {
  unreachable;
  // vm_area_t *area = vm_get_vm_area((uintptr_t) addr);
  // if (area == NULL || len == 0) {
  //   ERRNO = EINVAL;
  //   return -1;
  // }
  //
  // if (!(area->attr & AREA_MMAP)) {
  //   ERRNO = EINVAL;
  //   return -1;
  // }
  //
  // return vm_unmap((uintptr_t) addr, len);
}
