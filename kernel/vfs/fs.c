//
// Created by Aaron Gill-Braun on 2023-05-27.
//

#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/device.h>
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

#define goto_res(lbl, err) do { res = err; goto lbl; } while (0)
#define FTABLE (curproc->files)

#define HMAP_TYPE fs_type_t *
#include <hash_map.h>

hash_map_t *fs_types;
mtx_t fs_types_lock;
vcache_t *fs_vcache;
ventry_t *fs_root_ve;

static void fs_early_init() {
  fs_types = hash_map_new();
  mtx_init(&fs_types_lock, MTX_SPIN, "fs_types_lock");
}
EARLY_INIT(fs_early_init);

//

void fs_init() {
  DPRINTF("initializing\n");
  int res;

  // mount ramfs as root
  fs_type_t *ramfs_type = hash_map_get(fs_types, "ramfs");
  if (ramfs_type == NULL) {
    panic("ramfs not registered");
  }

  // create root vnode (will be shadowed)
  vnode_t *root_vn = vn_alloc(0, &make_vattr(V_DIR, 0755 | S_IFDIR));
  root_vn->state = V_ALIVE;
  // create root ventry
  fs_root_ve = ve_alloc_linked(cstr_make("/"), root_vn);
  fs_root_ve->state = V_ALIVE;
  fs_root_ve->flags |= VE_FSROOT;

  DPRINTF("created root ventry {:+ve}\n", fs_root_ve);

  // create root filesystem and mount it
  vfs_t *vfs = vfs_alloc(ramfs_type, 0);
  if ((res = vfs_mount(vfs, NULL, fs_root_ve)) < 0) {
    panic("failed to mount root fs");
  }

  fs_root_ve->parent = ve_getref(fs_root_ve);
  fs_vcache = vcache_alloc(fs_root_ve);

  vn_release(&root_vn);
  vfs_release(&vfs);

  curproc->pwd = ve_getref(fs_root_ve);
}

void fs_setup_final() {
  // must be called after fs_init and all module initializers have ran
  // this function sets up the initial filesystem structure and mounts the initrd (if available)
  int res;

  if (boot_info_v2->initrd_addr != 0) {
    // there is an initrd
    if ((res = fs_mkdir(cstr_make("/initrd"), 0777)) < 0) {
      panic("fs_setup_final: failed to create /initrd directory [{:err}]", res);
    }
    if ((res = fs_mknod(cstr_make("/rd0"), S_IFBLK, makedev(1, 0))) < 0) {
      panic("fs_setup_final: failed to create /rd0 block device [{:err}]", res);
    }

    // mount the initrd and replace root
    if ((res = fs_mount(cstr_make("/rd0"), cstr_make("/initrd"), "initrd", 0)) < 0) {
      panic("fs_setup_final: failed to mount initrd [{:err}]", res);
    }
    if ((res = fs_replace_root(cstr_make("/initrd"))) < 0) {
      panic("fs_setup_final: failed to replace root with initrd [{:err}]", res);
    }
    if ((res = fs_unmount(cstr_make("/"))) < 0) {
      panic("fs_setup_final: failed to unmount original root [{:err}]", res);
    }
  }

 // create /dev directory and special files
  if ((res = fs_mkdir(cstr_make("/dev"), 0777)) < 0) {
    panic("fs_setup_final: failed to create /dev directory [{:err}]", res);
  }
  if ((res = fs_mknod(cstr_make("/dev/stdin"), S_IFCHR, makedev(3, 0))) < 0) {
    panic("fs_setup_final: failed to create /dev/stdin character device [{:err}]", res);
  }
  if ((res = fs_mknod(cstr_make("/dev/stdout"), S_IFCHR, makedev(2, 0))) < 0) {
    panic("fs_setup_final: failed to create /dev/stdout character device [{:err}]", res);
  }
  if ((res = fs_mknod(cstr_make("/dev/stderr"), S_IFCHR, makedev(2, 3))) < 0) {
    panic("fs_setup_final: failed to create /dev/stderr character device [{:err}]", res);
  }

  DPRINTF("fs_setup_final completed successfully\n");
}

//


int fs_register_type(fs_type_t *fs_type) {
  if (hash_map_get(fs_types, fs_type->name) != NULL) {
    DPRINTF("fs type '%s' already registered\n", fs_type->name);
    return -1;
  }

  DPRINTF("registering fs type '%s'\n", fs_type->name);
  mtx_spin_lock(&fs_types_lock);
  hash_map_set(fs_types, fs_type->name, fs_type);
  mtx_spin_unlock(&fs_types_lock);
  return 0;
}

fs_type_t *fs_get_type(const char *type) {
  return hash_map_get(fs_types, type);
}

__ref ventry_t *fs_root_getref() {
  return ve_getref(fs_root_ve);
}

vm_file_t *fs_get_vm_file(int fd, size_t off, size_t len) {
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return NULL;

  vm_file_t *vm_file = vm_file_alloc_vnode(vn_getref(file->vnode), off, len);
  f_release(&file);
  return vm_file;
}

//

int fs_mount(cstr_t source, cstr_t mount, const char *fs_type, int flags) {
  fs_type_t *type = hash_map_get(fs_types, fs_type);
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  if (type == NULL) {
    DPRINTF("fs type '%s' not registered\n", fs_type);
    goto_res(ret, -ENODEV);
  }

  // resolve source device
  ventry_t *source_ve = NULL;
  if ((res = vresolve(fs_vcache, at_ve, source, VR_NOFOLLOW|VR_BLK, &source_ve)) < 0) {
    DPRINTF("failed to resolve source path\n");
    goto ret;
  }
  // hold lock only long enough to get the device
  device_t *device = VN(source_ve)->v_dev;
  ve_unlock_release(&source_ve);

  // lookup device
  if (device == NULL)
    goto_res(ret, -ENODEV);

  // resolve and lock mount point
  if ((res = vresolve(fs_vcache, at_ve, mount, VR_NOFOLLOW|VR_DIR, &mount_ve)) < 0) {
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

int fs_replace_root(cstr_t new_root) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *newroot_ve = NULL;
  int res;

  if (cstr_eq_charp(new_root, "/")) {
    DPRINTF("new_root cannot be root\n");
    goto_res(ret, -EINVAL);
  }

  // resolve new_root entry
  if ((res = vresolve(fs_vcache, at_ve, new_root, VR_NOFOLLOW|VR_DIR, &newroot_ve)) < 0) {
    DPRINTF("failed to resolve new_root path\n");
    goto ret;
  }
  if (!VE_ISMOUNT(newroot_ve)) {
    DPRINTF("new_root is not a mount point\n");
    ve_unlock(newroot_ve);
    goto_res(ret, -EINVAL);
  }

  // lock the fs root entry
  if (!ve_lock(fs_root_ve)) {
    DPRINTF("fs_root_ve is invalid\n");
    ve_unlock(newroot_ve);
    goto_res(ret, -EINVAL);
  }

  // perform the ventry pivot
  ve_replace_root(fs_root_ve, newroot_ve);
  // invalidate the vcache
  vcache_invalidate_all(fs_vcache);

  ve_unlock(fs_root_ve);
  ve_unlock(newroot_ve);

  res = 0; // success
LABEL(ret);
  ve_release(&newroot_ve);
  ve_release(&at_ve);
  return res;
}

int fs_unmount(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  // resolve and mount point and lock vfs
  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW|VR_DIR, &mount_ve)) < 0) {
    DPRINTF("failed to resolve mount path\n");
    return res;
  }

  vfs_t *vfs = vfs_getref(VN(mount_ve)->vfs);
  if (!vfs_lock(vfs)) {
    DPRINTF("vfs is dead\n");
    goto_res(ret, -EINVAL);
  }

  // unmount the vfs
  if ((res = vfs_unmount(vfs, mount_ve)) < 0) {
    DPRINTF("failed to unmount fs\n");
  }

  vfs_unlock(vfs);
  vfs_release(&vfs);

  res = 0; // success
LABEL(ret);
  ve_unlock(mount_ve);
  ve_release(&mount_ve);
  ve_release(&at_ve);
  return res;
}

//

int fs_proc_open(proc_t *proc, int fd, cstr_t path, int flags, mode_t mode) {
  ventry_t *at_ve = ve_getref(proc->pwd);
  ventry_t *ve = NULL;
  int res;

  if (fd < 0) {
    // allocate new fd
    fd = ftable_alloc_fd(proc->files);
    if (fd < 0)
      goto_res(ret, -EMFILE);
  } else {
    // claim fd
    if (ftable_claim_fd(proc->files, fd) < 0)
      goto_res(ret, -EBADF);
  }

  int acc = flags & O_ACCMODE;
  if (acc != O_RDONLY && acc != O_WRONLY && acc != O_RDWR)
    goto_res(ret, -EINVAL);

  int vrflags = VR_NOTDIR;
  if (flags & O_NOFOLLOW)
    vrflags |= VR_NOFOLLOW;
  if (flags & O_CREAT) {
    vrflags |= VR_PARENT;
    if (flags & O_EXCL)
      vrflags |= VR_EXCLUSV;
  }

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(path);

  // resolve the path
  res = vresolve_fullpath(fs_vcache, at_ve, path, vrflags, &rpath_buf, &ve);
  if (res < 0 && flags & O_CREAT) {
    if (ve == NULL)
      goto ret;

    // ve is current set to the locked parent directory
    ventry_t *dve = ve_moveref(&ve);
    vnode_t *dvn = VN(dve);
    vn_begin_data_write(dvn);
    res = vn_create(dve, dvn, name, mode, &ve); // create the file entry
    vn_end_data_write(dvn);
    vn_unlock(dvn);
    ve_release(&dve);
    if (res < 0) {
      DPRINTF("failed to create file\n");
      goto ret_unlock;
    }

    // cache the new entry
    sbuf_write_char(&rpath_buf, '/');
    sbuf_write_cstr(&rpath_buf, name);
    vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);
  } else if (res < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  }

  if (flags & VR_NOFOLLOW) {
    // check if the file is a symlink or mount
    if (V_ISLNK(ve) || VE_ISMOUNT(ve)) {
      goto_res(ret_unlock, -ELOOP);
    }
  }

  vnode_t *vn = VN(ve);
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    if (!device)
      goto_res(ret_unlock, -ENODEV);
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

  file_t *file = f_alloc(fd, flags, vn, cstr_from_sbuf(&rpath_buf));
  ftable_add_file(proc->files, f_moveref(&file));

  res = fd;
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  if (res < 0)
    ftable_free_fd(proc->files, fd);

  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_proc_close(proc_t *proc, int fd) {
  file_t *file = NULL;
  int res;

  file = ftable_get_file(proc->files, fd);
  if (file == NULL)
    return -EBADF;

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_res(ret_unlock, -EISDIR); // file is a directory

  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    if (!device)
      goto_res(ret_unlock, -ENODEV);
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
  // cond_broadcast(&vn->waiters);
  // todo(); // TODO:
  vn_unlock(vn);

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

int fs_open(cstr_t path, int flags, mode_t mode) {
  return fs_proc_open(curproc, -1, path, flags, mode);
}

int fs_close(int fd) {
  return fs_proc_close(curproc, fd);
}

__ref page_t *fs_getpage(int fd, off_t off) {
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return NULL;

  if (!f_lock(file))
    return NULL; // file is closed

  page_t *res = NULL;
  if (vn_getpage(file->vnode, off, /*pgcache=*/true, &res) < 0) {
    DPRINTF("failed to get page\n");
    res = NULL;
  }

  f_unlock(file);
  f_release(&file);
  return res;
}

ssize_t fs_kread(int fd, kio_t *kio) {
  ASSERT(kio->dir == KIO_WRITE);
  ssize_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_res(ret, -EISDIR); // file is a directory
  if (file->flags & O_WRONLY)
    goto_res(ret, -EBADF); // file is not open for reading

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    if (!device)
      goto_res(ret_unlock, -ENODEV);
    if (!device->ops->d_read)
      goto_res(ret_unlock, -ENOTSUP);

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

ssize_t fs_kwrite(int fd, kio_t *kio) {
  ASSERT(kio->dir == KIO_READ);
  ssize_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(file->vnode))
    goto_res(ret, -EISDIR); // file is a directory
  if (file->flags & O_RDONLY)
    goto_res(ret, -EBADF); // file is not open for writing

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  if (file->flags & O_APPEND)
    file->offset = (off_t) vn->size;

  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    if (!device)
      goto_res(ret_unlock, -ENODEV);
    if (!device->ops->d_write)
      goto_res(ret_unlock, -ENOTSUP);

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
  kio_t kio = kio_new_writable(buf, len);
  return fs_kread(fd, &kio);
}

ssize_t fs_write(int fd, const void *buf, size_t len) {
  kio_t kio = kio_new_readable(buf, len);
  return fs_kwrite(fd, &kio);
}

ssize_t fs_readv(int fd, const struct iovec *iov, int iovcnt) {
  if (iovcnt <= 0)
    return -EINVAL;

  kio_t kio = kio_new_writablev(iov, (uint32_t) iovcnt);
  return fs_kread(fd, &kio);
}

ssize_t fs_writev(int fd, const struct iovec *iov, int iovcnt) {
  if (iovcnt <= 0)
    return -EINVAL;

  kio_t kio = kio_new_readablev(iov, (uint32_t) iovcnt);
  return fs_kwrite(fd, &kio);
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  off_t res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (V_ISDIR(vn))
    goto_res(ret, -EISDIR); // file is a directory
  if (V_ISFIFO(vn) || V_ISSOCK(vn))
    goto_res(ret_unlock, -ESPIPE); // file is a pipe or socket

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

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
      goto_res(ret_unlock, -EINVAL); // invalid whence
  }

  if (res < 0 || res > (off_t) file->vnode->size)
    goto_res(ret_unlock, -EINVAL); // invalid offset

  // update the file offset
  file->offset = res;

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  f_release(&file);
  return res;
}

int fs_ioctl(int fd, unsigned long request, void *argp) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  DPRINTF("ioctl(%d, %#llx, %p)\n", fd, request, argp);
  DPRINTF("TODO: implement ioct (%s:%d)\n", __FILE__, __LINE__);

  res = -EOPNOTSUPP;
LABEL(ret);
  f_release(&file);
  return res;
}

//

int fs_chdir(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW|VR_DIR, &ve)) < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  } else if (ve != at_ve) {
    ve_unlock(ve);
    ve_release_swap(&curproc->pwd, &ve);
  } else {
    ve_unlock(ve);
  }

  res = 0; // success
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_opendir(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);

  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_DIR, &rpath_buf, &ve)) < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  }

  int fd = ftable_alloc_fd(FTABLE);
  if (fd < 0)
    goto_res(ret_unlock, -EMFILE);

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn->nopen++;
  vn_unlock(vn);

  file_t *file = f_alloc(fd, O_RDONLY, vn, cstr_from_sbuf(&rpath_buf));
  ftable_add_file(FTABLE, f_moveref(&file));

  res = fd; // success
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_closedir(int fd) {
  int res;
  file_t *file = ftable_get_file(FTABLE, fd);
  if (file == NULL)
    return -EBADF;

  vnode_t *vn = file->vnode;
  if (!V_ISDIR(vn))
    goto_res(ret, -ENOTDIR); // file is not a directory

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  ftable_remove_file(FTABLE, fd);
  ftable_free_fd(FTABLE, fd);
  file->fd = -1;
  file->closed = true;

  vn_lock(vn);
  vn->nopen--;
  // cond_broadcast(&vn->waiters);
  // todo(); // TODO:
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
    goto_res(ret, -ENOTDIR); // file is not a directory

  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  // read the directory
  kio_t kio = kio_new_writable(dirp, len);
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
    goto_res(ret, -ENOTDIR); // file is not a directory

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
    goto_res(ret, -EBADF); // file is closed

  int newfd = ftable_alloc_fd(FTABLE);
  if (newfd < 0)
    goto_res(ret_unlock, -EMFILE);

  int newflags = file->flags & ~O_CLOEXEC;
  file_t *newfile = f_alloc(newfd, newflags, file->vnode, cstr_from_str(file->real_path));
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
    goto_res(ret, -EBADF); // file is closed

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

int fs_stat(cstr_t path, struct stat *stat) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(fs_vcache, at_ve, path, 0, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn_stat(vn, stat);
  vn_unlock(vn);

  res = 0; // success
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_lstat(cstr_t path, struct stat *stat) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn_stat(vn, stat);
  vn_unlock(vn);

  res = 0; // success
LABEL(ret);
  ve_release(&ve);
  ve_release(&at_ve);
  return res;
}

int fs_create(cstr_t path, mode_t mode) {
  return fs_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fs_mknod(cstr_t path, mode_t mode, dev_t dev) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(path);

  // resolve the parent directory
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_EXCLUSV, &rpath_buf, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_mknod(dve, dvn, name, mode, dev, &ve); // create the node
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create node\n");
    goto ret_unlock;
  }

  // cache the new entry
  sbuf_write_char(&rpath_buf, '/');
  sbuf_write_cstr(&rpath_buf, name);
  vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);

  ve_release(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_symlink(cstr_t target, cstr_t linkpath) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(linkpath);

  // resolve the parent directory
  if ((res = vresolve_fullpath(fs_vcache, at_ve, linkpath, VR_EXCLUSV, &rpath_buf, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_symlink(dve, dvn, name, target, &ve); // create the symlink
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create symlink\n");
    goto ret_unlock;
  }

  // cache the new entry
  sbuf_write_char(&rpath_buf, '/');
  sbuf_write_cstr(&rpath_buf, name);
  vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);

  ve_release(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_link(cstr_t oldpath, cstr_t newpath) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ove = NULL;
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(newpath);

  // resolve the oldpath
  if ((res = vresolve(fs_vcache, at_ve, oldpath, VR_NOTDIR, &ove)) < 0)
    goto ret;

  // resolve the parent directory
  if ((res = vresolve_fullpath(fs_vcache, at_ve, newpath, VR_EXCLUSV, &rpath_buf, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vnode_t *ovn = VN(ove);
  vn_lock(ovn);
  vn_begin_data_write(dvn);
  res = vn_hardlink(dve, dvn, cstr_basename(newpath), ovn, &ve); // create the hard link
  vn_end_data_write(dvn);
  vn_unlock(ovn);
  if (res < 0) {
    DPRINTF("failed to create hard link\n");
    goto ret_unlock;
  }

  // cache the new entry
  sbuf_write_char(&rpath_buf, '/');
  sbuf_write_cstr(&rpath_buf, name);
  vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);

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

int fs_unlink(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);

  // resolve the path
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, 0, &rpath_buf, &ve)) < 0)
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

  vcache_invalidate(fs_vcache, cstr_from_sbuf(&rpath_buf));
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

int fs_mkdir(cstr_t path, mode_t mode) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(path);

  // resolve the parent directory
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_EXCLUSV, &rpath_buf, &dve)) < 0)
    goto ret;

  ventry_t *ve = NULL;
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  res = vn_mkdir(dve, dvn, name, mode, &ve); // create the directory
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to create directory\n");
    goto ret_unlock;
  }

  // cache the new entry
  sbuf_write_char(&rpath_buf, '/');
  sbuf_write_cstr(&rpath_buf, name);
  vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);

  ve_release(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_release(&dve);
  ve_release(&at_ve);
  return res;
}

int fs_rmdir(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);

  // resolve the path
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, 0, &rpath_buf, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  if (vn->nlink > 2) {
    ve_unlock(ve);
    goto_res(ret, -ENOTEMPTY);
  }

  dve = ve_getref(ve->parent);
  vnode_t *dvn = VN(dve);
  vn_begin_data_write(dvn);
  ve_lock(dve);
  vn_lock(vn);
  res = vn_rmdir(dve, dvn, ve, vn); // remove the directory
  vn_unlock(vn);
  vn_end_data_write(dvn);
  if (res < 0) {
    DPRINTF("failed to remove directory\n");
    goto ret_unlock;
  }

  vcache_invalidate(fs_vcache, cstr_from_sbuf(&rpath_buf));
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

int fs_rename(cstr_t oldpath, cstr_t newpath) {
  unimplemented("rename");
}

ssize_t fs_readlink(cstr_t path, char *buf, size_t bufsiz) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  ssize_t res;

  if ((res = vresolve(fs_vcache, at_ve, path, VR_LNK, &ve)) < 0)
    goto ret;

  kio_t kio = kio_new_writable(buf, bufsiz);
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


void fs_print_debug_vcache() {
  vcache_dump(fs_vcache);
}

// MARK: System Calls

SYSCALL_ALIAS(close, fs_close);
SYSCALL_ALIAS(read, fs_read);
SYSCALL_ALIAS(write, fs_write);
SYSCALL_ALIAS(readv, fs_readv);
SYSCALL_ALIAS(writev, fs_writev);
SYSCALL_ALIAS(lseek, fs_lseek);
SYSCALL_ALIAS(fstat, fs_fstat);
SYSCALL_ALIAS(stat, fs_stat);
SYSCALL_ALIAS(ioctl, fs_ioctl);

DEFINE_SYSCALL(open, int, const char *path, int flags, mode_t mode) {
  return fs_open(cstr_make(path), flags, mode);
}

DEFINE_SYSCALL(lstat, int, const char *path, struct stat *stat) {
  return fs_lstat(cstr_make(path), stat);
}
