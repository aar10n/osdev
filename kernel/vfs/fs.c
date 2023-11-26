//
// Created by Aaron Gill-Braun on 2023-05-27.
//

#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/device.h>
#include <kernel/process.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/str.h>
#include <kernel/kio.h>

#include <kernel/vfs/file.h>
#include <kernel/vfs/vcache.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/vresolve.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("fs: %s: " fmt, __func__, ##__VA_ARGS__)
#define goto_error(lbl, err) do { res = err; goto lbl; } while (0)

#define FTABLE (PERCPU_PROCESS->files)

#define HMAP_TYPE fs_type_t *
#include <hash_map.h>

hash_map_t *fs_types;
spinlock_t fs_types_lock;
vcache_t *vcache;
ventry_t *root_ve;

//

void fs_early_init() {
  fs_types = hash_map_new();
  spin_init(&fs_types_lock);
}

void fs_init() {
  DPRINTF("initializing\n");
  int res;

  // mount ramfs as root
  fs_type_t *ramfs_type = hash_map_get(fs_types, "ramfs");
  if (ramfs_type == NULL) {
    panic("ramfs not registered");
  }

  // create root ventry and root vnode (will be shadowed)
  vnode_t *root_vn = vn_alloc(0, &make_vattr(V_DIR, 0755 | S_IFDIR));
  root_vn->state = V_ALIVE;
  root_ve = ve_alloc_linked(cstr_make("/"), root_vn);
  root_ve->state = V_ALIVE;

  // create root filesystem and mount it
  vfs_t *vfs = vfs_alloc(ramfs_type, 0);
  if ((res = vfs_mount(vfs, NULL, root_ve)) < 0) {
    panic("failed to mount root fs");
  }

  root_ve->vfs_id = vfs->id;
  root_ve->parent = ve_getref(root_ve);

  vcache = vcache_alloc(root_ve);
  vcache_put(vcache, cstr_new("/", 1), root_ve);
}

int fs_register_type(fs_type_t *fs_type) {
  if (hash_map_get(fs_types, fs_type->name) != NULL) {
    DPRINTF("fs type '%s' already registered\n", fs_type->name);
    return -1;
  }

  DPRINTF("registering fs type '%s'\n", fs_type->name);
  SPIN_LOCK(&fs_types_lock);
  hash_map_set(fs_types, fs_type->name, fs_type);
  SPIN_UNLOCK(&fs_types_lock);
  return 0;
}

fs_type_t *fs_get_type(const char *type) {
  return hash_map_get(fs_types, type);
}

__move ventry_t *fs_root_getref() {
  return ve_getref(root_ve);
}

//

int fs_mount(const char *source, const char *mount, const char *fs_type, int flags) {
  fs_type_t *type = hash_map_get(fs_types, fs_type);
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  if (type == NULL) {
    DPRINTF("fs type '%s' not registered\n", fs_type);
    goto_error(ret, -ENODEV);
  }

  // resolve source device
  ventry_t *source_ve = NULL;
  if ((res = vresolve(vcache, at_ve, cstr_make(source), VR_NOFOLLOW|VR_BLK, &source_ve)) < 0) {
    DPRINTF("failed to resolve source path\n");
    goto ret;
  }
  dev_t dev = VN(source_ve)->v_dev; // hold lock only long enough to get device number
  ve_unlock_release(&source_ve);

  // lookup device
  device_t *device = device_get(dev);
  if (device == NULL)
    goto_error(ret, -ENODEV);

  // resolve and lock mount point
  cstr_t mount_str = cstr_make(mount);
  if ((res = vresolve(vcache, at_ve, mount_str, VR_NOFOLLOW|VR_DIR, &mount_ve)) < 0) {
    DPRINTF("failed to resolve mount path\n");
    goto ret;
  }

  // create new vfs and mount it
  vfs_t *vfs = vfs_alloc(type, flags);
  if ((res = vfs_mount(vfs, device, mount_ve)) < 0) {
    DPRINTF("failed to mount fs\n");
    vfs_release(&vfs);
    goto ret_unlock;
  }
  vfs_release(&vfs);

LABEL(ret_unlock);
  ve_unlock(mount_ve);
LABEL(ret);
  ve_release(&mount_ve);
  ve_release(&at_ve);
  return res;
}

int fs_unmount(const char *path) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  // resolve and lock mount point
  if ((res = vresolve(vcache, at_ve, cstr_make(path), VR_NOFOLLOW|VR_DIR, &mount_ve)) < 0) {
    DPRINTF("failed to resolve mount path\n");
    return res;
  }

  // unmount
  if ((res = vfs_unmount(VN(mount_ve)->vfs, mount_ve)) < 0) {
    DPRINTF("failed to unmount fs\n");
    goto ret;
  }

LABEL(ret);
  ve_release(&mount_ve);
  ve_release(&at_ve);
  return res;
}

//

int fs_open(const char *path, int flags, mode_t mode) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  int fd = -1;
  int res;

  fd = ftable_alloc_fd(FTABLE);
  if (fd < 0)
    goto_error(ret, -EMFILE);

  int acc = flags & O_ACCMODE;
  if (acc != O_RDONLY && acc != O_WRONLY && acc != O_RDWR)
    goto_error(ret, -EINVAL);

  int vrflags = VR_NOTDIR;
  if (flags & O_NOFOLLOW)
    vrflags |= VR_NOFOLLOW;
  if (flags & O_CREAT) {
    vrflags |= VR_PARENT;
    if (flags & O_EXCL)
      vrflags |= VR_EXCLUSV;
  }

  // resolve the path
  cstr_t pathstr = cstr_make(path);
  res = vresolve(vcache, at_ve, pathstr, vrflags, &ve);
  if (res < 0 && flags & O_CREAT) {
    if (ve == NULL)
      goto ret;

    // ve is current set to the locked parent directory
    ventry_t *dve = ve_moveref(&ve);
    vnode_t *dvn = VN(dve);
    vn_begin_data_write(dvn);
    res = vn_create(dve, dvn, cstr_basename(pathstr), mode, &ve); // create the file entry
    vn_end_data_write(dvn);
    vn_unlock(dvn);
    ve_release(&dve);
    if (res < 0) {
      DPRINTF("failed to create file\n");
      goto ret_unlock;
    }

    vcache_put(vcache, pathstr, ve);
  } else if (res < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  }

  if (flags & VR_NOFOLLOW) {
    // check if the file is a symlink or mount
    if (V_ISLNK(ve) || VE_ISMOUNT(ve)) {
      goto_error(ret_unlock, -ELOOP);
    }
  }

  vnode_t *vn = VN(ve);
  if (V_ISDEV(vn)) {
    device_t *device = device_get(vn->v_dev);
    if (!device)
      goto_error(ret_unlock, -ENODEV);
    if (!device->ops->d_open) {
      res = 0;
      goto done;
    }

    // device open
    res = device->ops->d_open(device, flags);
    if (res < 0) {
      DPRINTF("failed to open device\n");
      goto ret_unlock;
    }
    goto done;
  }

  // open the file
  if ((res = vn_open(vn, flags)) < 0)
    goto ret_unlock;

LABEL(done);
  vn_lock(vn);
  vn->nopen++;
  vn_unlock(vn);

  file_t *file = f_alloc(fd, flags, vn);
  ftable_add_file(FTABLE, f_moveref(&file));

  res = fd;
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  if (res < 0)
    ftable_free_fd(FTABLE, fd);

  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_close(int fd) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_error(ret_unlock, -EISDIR); // file is a directory

  if (V_ISDEV(vn)) {
    device_t *device = device_get(vn->v_dev);
    if (!device)
      goto_error(ret_unlock, -ENODEV);
    if (!device->ops->d_close) {
      res = 0;
      goto done;
    }

    // device close
    res = device->ops->d_close(device);
    if (res < 0) {
      DPRINTF("failed to close device\n");
      goto ret_unlock;
    }
    goto done;
  }

  // close the file
  if ((res = vn_close(vn)) < 0) {
    DPRINTF("failed to close file\n");
    goto ret_unlock;
  }

LABEL(done);
  // release the file descriptor
  ftable_remove_file(FTABLE, fd);
  ftable_free_fd(FTABLE, fd);
  file->fd = -1;
  file->closed = true;

  vn_lock(vn);
  vn->nopen--;
  cond_broadcast(&vn->waiters);
  vn_unlock(vn);

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

ssize_t fs_read_kio(int fd, kio_t *kio) {
  ssize_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_error(ret, -EISDIR); // file is a directory
  if (file->flags & O_WRONLY)
    goto_error(ret, -EBADF); // file is not open for reading

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  if (V_ISDEV(vn)) {
    device_t *device = device_get(vn->v_dev);
    if (!device)
      goto_error(ret_unlock, -ENODEV);
    if (!device->ops->d_read)
      goto_error(ret_unlock, -ENOTSUP);

    // device read
    res = d_read(device, file->offset, kio);
    if (res < 0) {
      DPRINTF("failed to read device\n");
      goto ret_unlock;
    }
    goto done;
  }

  // read the file
  vn_begin_data_read(vn);
  res = vn_read(vn, file->offset, kio);
  vn_end_data_read(vn);
  if (res < 0) {
    DPRINTF("failed to read file\n");
    goto ret_unlock;
  }

LABEL(done);
  // update the file offset
  file->offset += res;

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

ssize_t fs_write_kio(int fd, kio_t *kio) {
  ssize_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(file->vnode))
    goto_error(ret, -EISDIR); // file is a directory
  if (file->flags & O_RDONLY)
    goto_error(ret, -EBADF); // file is not open for writing

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  if (file->flags & O_APPEND)
    file->offset = (off_t) vn->size;

  if (V_ISDEV(vn)) {
    device_t *device = device_get(vn->v_dev);
    if (!device)
      goto_error(ret_unlock, -ENODEV);
    if (!device->ops->d_write)
      goto_error(ret_unlock, -ENOTSUP);

    // device write
    res = d_write(device, file->offset, kio);
    if (res < 0) {
      DPRINTF("failed to write device\n");
      goto ret_unlock;
    }
    goto done;
  }

  // write the file
  vn_begin_data_write(vn);
  res = vn_write(vn, file->offset, kio);
  vn_end_data_write(vn);
  if (res < 0) {
    DPRINTF("failed to write file\n");
    goto ret_unlock;
  }

LABEL(done);
  // update the file offset
  file->offset += res;

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

ssize_t fs_read(int fd, void *buf, size_t len) {
  kio_t kio = kio_new_write(buf, len);
  return fs_read_kio(fd, &kio);
}

ssize_t fs_write(int fd, const void *buf, size_t len) {
  kio_t kio = kio_new_read(buf, len);
  return fs_write_kio(fd, &kio);
}

ssize_t fs_readv(int fd, const struct iovec *iov, int iovcnt) {
  if (iovcnt <= 0)
    return -EINVAL;

  DPRINTF("readv: %d\n", iovcnt);
  kio_t kio = kio_new_writev(iov, (uint32_t) iovcnt);
  return fs_read_kio(fd, &kio);
}

ssize_t fs_writev(int fd, const struct iovec *iov, int iovcnt) {
  if (iovcnt <= 0)
    return -EINVAL;

  DPRINTF("writev: %d\n", iovcnt);
  kio_t kio = kio_new_readv(iov, (uint32_t) iovcnt);
  return fs_read_kio(fd, &kio);
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  off_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_error(ret, -EISDIR); // file is a directory
  if (V_ISFIFO(vn) || V_ISSOCK(vn))
    goto_error(ret_unlock, -ESPIPE); // file is a pipe or socket

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  // update the file offset
  switch (whence) {
    case SEEK_SET:
      res = offset;
      break;
    case SEEK_CUR:
      res = file->offset + offset;
      break;
    case SEEK_END:
      res = (off_t) file->vnode->size + offset;
      break;
    default:
      goto_error(ret_unlock, -EINVAL); // invalid whence
  }

  if (res < 0 || res > (off_t) file->vnode->size)
    goto_error(ret_unlock, -EINVAL); // invalid offset

  // update the file offset
  file->offset = res;

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

//

int fs_opendir(const char *path) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(vcache, at_ve, cstr_make(path), VR_DIR, &ve)) < 0) {
    DPRINTF("failed to resolve path\n");
    return res;
  }

  int fd = ftable_alloc_fd(FTABLE);
  if (fd < 0)
    goto_error(ret, -EMFILE);

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn->nopen++;
  vn_unlock(vn);

  file_t *file = f_alloc(fd, O_RDONLY, vn);
  ftable_add_file(FTABLE, f_moveref(&file));

  res = fd; // success
LABEL(ret);
  ve_unlock(ve);
  ve_release(&ve);
  ve_release(&at_ve);
  return fd;
}

int fs_closedir(int fd) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (!V_ISDIR(vn))
    goto_error(ret, -ENOTDIR); // file is not a directory

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  ftable_remove_file(FTABLE, fd);
  ftable_free_fd(FTABLE, fd);
  file->fd = -1;
  file->closed = true;

  vn_lock(vn);
  vn->nopen--;
  cond_broadcast(&vn->waiters);
  vn_unlock(vn);
  f_unlock(file);

  res = 0; // success
LABEL(ret);
  f_release(&file);
  return res;
}

ssize_t fs_readdir(int fd, void *dirp, size_t len) {
  ssize_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (!V_ISDIR(vn))
    goto_error(ret, -ENOTDIR); // file is not a directory

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  // read the directory
  kio_t kio = kio_new_write(dirp, len);
  vn_begin_data_read(vn);
  res = vn_readdir(vn, file->offset, &kio);
  vn_end_data_read(vn);
  if (res < 0) {
    DPRINTF("failed to read directory\n");
    goto ret_unlock;
  }

  // update the file offset
  file->offset += res;

  res = (ssize_t) kio_transfered(&kio);
  kio_remfill(&kio, 0);
  // success
LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

long fs_telldir(int fd) {
  long res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  if (!V_ISDIR(file->vnode))
    goto_error(ret, -ENOTDIR); // file is not a directory

  res = file->offset;
LABEL(ret);
  f_release(&file);
  return res;
}

void fs_seekdir(int fd, long loc) {
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return;

  if (!V_ISDIR(file->vnode))
    goto ret; // file is not a directory

  file->offset = loc;
LABEL(ret);
  f_release(&file);
}

//

int fs_dup(int fd) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  int newfd = ftable_alloc_fd(FTABLE);
  if (newfd < 0)
    goto_error(ret_unlock, -EMFILE);

  int newflags = file->flags & ~O_CLOEXEC;
  file_t *newfile = f_alloc(newfd, newflags, file->vnode);
  ftable_add_file(FTABLE, newfile);

  res = newfd; // success
LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

int fs_dup2(int fd, int newfd) {
  // TODO: implement
  unimplemented("dup2");
}

int fs_fstat(int fd, struct stat *stat) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  if (!f_lock(file))
    goto_error(ret, -EBADF); // file is closed

  vnode_t *vn = file->vnode;
  vn_lock(vn);
  vn_stat(vn, stat);
  vn_unlock(vn);
  f_unlock(file);

  res = 0; // success
LABEL(ret);
  f_release(&file);
  return res;
}

//

int fs_stat(const char *path, struct stat *stat) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(vcache, at_ve, cstr_make(path), 0, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn_stat(vn, stat);
  vn_unlock(vn);

  res = 0; // success
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return 0;
}

int fs_lstat(const char *path, struct stat *stat) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(vcache, at_ve, cstr_make(path), VR_NOFOLLOW, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn_stat(vn, stat);
  vn_unlock(vn);

  res = 0; // success
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return 0;
}

int fs_create(const char *path, mode_t mode) {
  return fs_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fs_mknod(const char *path, mode_t mode, dev_t dev) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *dve = NULL;
  int res;

  // resolve the parent directory
  cstr_t pathstr = cstr_make(path);
  if ((res = vresolve(vcache, at_ve, pathstr, VR_EXCLUSV, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_mknod(dve, dvn, cstr_basename(pathstr), mode, dev, &ve); // create the node
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create node\n");
    goto ret_unlock;
  }

  vcache_put(vcache, pathstr, ve);
  ve_release(&ve);

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_symlink(const char *target, const char *linkpath) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *dve = NULL;
  int res;

  // resolve the parent directory
  cstr_t pathstr = cstr_make(linkpath);
  if ((res = vresolve(vcache, at_ve, pathstr, VR_EXCLUSV, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_symlink(dve, dvn, cstr_basename(pathstr), cstr_make(target), &ve); // create the symlink
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create symlink\n");
    goto ret_unlock;
  }

  vcache_put(vcache, pathstr, ve);
  ve_release(&ve);

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_link(const char *oldpath, const char *newpath) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ove = NULL;
  ventry_t *dve = NULL;
  int res;

  // resolve the oldpath
  if ((res = vresolve(vcache, at_ve, cstr_make(oldpath), VR_NOTDIR, &ove)) < 0)
    goto ret;

  // resolve the parent directory
  cstr_t pathstr = cstr_make(newpath);
  if ((res = vresolve(vcache, at_ve, pathstr, VR_EXCLUSV, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vnode_t *ovn = VN(ove);
  vn_lock(ovn);
  vn_begin_data_write(dvn);
  res = vn_hardlink(dve, dvn, cstr_basename(pathstr), ovn, &ve); // create the hard link
  vn_end_data_write(dvn);
  vn_unlock(ovn);
  if (res < 0) {
    DPRINTF("failed to create hard link\n");
    goto ret_unlock;
  }

  vcache_put(vcache, pathstr, ve);
  ve_release(&ve);

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ove);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&ove);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_unlink(const char *path) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  ventry_t *dve = NULL;
  int res;

  // resolve the path
  cstr_t pathstr = cstr_make(path);
  if ((res = vresolve(vcache, at_ve, pathstr, 0, &ve)) < 0)
    goto ret;

  // lock the parent directory
  dve = ve_getref(ve->parent);
  ve_lock(dve);

  vnode_t *dvn = VN(dve);
  vnode_t *vn = VN(ve);
  vn_begin_data_write(dvn);
  vn_lock(vn);
  res = vn_unlink(dve, dvn, ve, vn); // unlink the node
  vn_unlock(vn);
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to unlink file\n");
    goto ret_unlock;
  }

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ve);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&ve);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_mkdir(const char *path, mode_t mode) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *dve = NULL;
  int res;

  // resolve the parent directory
  cstr_t pathstr = cstr_make(path);
  if ((res = vresolve(vcache, at_ve, pathstr, VR_EXCLUSV, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_mkdir(dve, dvn, cstr_basename(pathstr), mode, &ve); // create the directory
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create directory\n");
    goto ret_unlock;
  }

  vcache_put(vcache, pathstr, ve);
  ve_release(&ve);

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_rmdir(const char *path) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  ventry_t *dve = NULL;
  int res;

  // resolve the path
  if ((res = vresolve(vcache, at_ve, cstr_make(path), 0, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  if (vn->nlink > 2) {
    ve_unlock(ve);
    goto_error(ret, -ENOTEMPTY);
  }

  dve = ve_getref(ve->parent);
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  ve_lock(dve);
  vn_lock(vn);
  res = vn_rmdir(dve, dvn, ve, vn); // remove the directory
  vn_unlock(vn);
  ve_unlock(ve);
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to remove directory\n");
    goto ret_unlock;
  }

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ve);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&ve);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_rename(const char *oldpath, const char *newpath) {
  unimplemented("rename");
}

ssize_t fs_readlink(const char *path, char *buf, size_t bufsiz) {
  ventry_t *at_ve = ve_getref(PERCPU_PROCESS->pwd);
  ventry_t *ve = NULL;
  ssize_t res;

  if ((res = vresolve(vcache, at_ve, cstr_make(path), VR_LNK, &ve)) < 0)
    goto ret;

  kio_t kio = kio_new_write(buf, bufsiz);
  vnode_t *vn = VN(ve);
  vn_begin_data_read(vn);
  res = vn_readlink(vn, &kio); // read the link
  vn_end_data_read(vn);
  if (res < 0) {
    DPRINTF("failed to read link\n");
    goto ret_unlock;
  }

  // success
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

//

void fs_print_debug_vcache() {
  vcache_dump(vcache);
}

//
// MARK: Syscalls
//

DEFINE_SYSCALL(open, int, const char *, int, mode_t) alias("fs_open");
DEFINE_SYSCALL(close, int, int) alias("fs_close");
DEFINE_SYSCALL(read, ssize_t, int, void *, size_t) alias("fs_read");
DEFINE_SYSCALL(write, ssize_t, int, const void *, size_t) alias("fs_write");
DEFINE_SYSCALL(readv, ssize_t, int, const struct iovec *, int) alias("fs_readv");
DEFINE_SYSCALL(writev, ssize_t, int, const struct iovec *, int) alias("fs_writev");
DEFINE_SYSCALL(lseek, off_t, int, off_t, int) alias("fs_lseek");
DEFINE_SYSCALL(fstat, int, int, struct stat *) alias("fs_fstat");
DEFINE_SYSCALL(stat, int, const char *, struct stat *) alias("fs_stat");
DEFINE_SYSCALL(lstat, int, const char *, struct stat *) alias("fs_lstat");
