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
#include <kernel/time.h>

#include <kernel/vfs/file.h>
#include <kernel/vfs/pipe.h>
#include <kernel/vfs/vcache.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/vresolve.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("fs: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("fs: %s: " fmt, __func__, ##__VA_ARGS__)

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

  vn_putref(&root_vn);
  vfs_putref(&vfs);

  curproc->pwd = ve_getref(fs_root_ve);
}

void fs_setup_mounts() {
  // must be called after fs_init and all module initializers have ran
  // this function sets up the initial filesystem structure and mounts
  // the initrd (if available)
  int res;

  if (boot_info_v2->initrd_addr != 0) {
    // there is an initrd
    if ((res = fs_mkdir(cstr_make("/initrd"), 0777)) < 0) {
      panic("fs_setup_mounts: failed to create /initrd directory [{:err}]", res);
    }
    if ((res = fs_mknod(cstr_make("/rd0"), S_IFBLK, makedev(1, 0))) < 0) {
      panic("fs_setup_mounts: failed to create /rd0 block device [{:err}]", res);
    }

    // mount the initrd and replace root
    if ((res = fs_mount(cstr_make("/rd0"), cstr_make("/initrd"), "initrd", 0)) < 0) {
      panic("fs_setup_mounts: failed to mount initrd [{:err}]", res);
    }
    if ((res = fs_replace_root(cstr_make("/initrd"))) < 0) {
      panic("fs_setup_mounts: failed to replace root with initrd [{:err}]", res);
    }
    if ((res = fs_unmount(cstr_make("/"))) < 0) {
      panic("fs_setup_mounts: failed to unmount original root [{:err}]", res);
    }
  }

  // mount devfs at /dev
  if ((res = fs_mkdir(cstr_make("/dev"), 0777)) < 0) {
    panic("fs_setup_mounts: failed to create /dev directory [{:err}]", res);
  }
  if ((res = fs_mknod(cstr_make("/loop"), S_IFBLK, makedev(4, 0))) < 0) {
    panic("fs_setup_mounts: failed to create /dev/loop block device [{:err}]", res);
  }
  if ((res = fs_mount(cstr_make("/loop"), cstr_make("/dev"), "devfs", 0)) < 0) {
    panic("fs_setup_mounts: failed to mount devfs [{:err}]", res);
  }

  // mount procfs at /proc
  if ((res = fs_mkdir(cstr_make("/proc"), 0777)) < 0) {
    panic("fs_setup_mounts: failed to create /proc directory [{:err}]", res);
  }
  if ((res = fs_mount(cstr_make("/loop"), cstr_make("/proc"), "procfs", 0)) < 0) {
    panic("fs_setup_mounts: failed to mount procfs [{:err}]", res);
  }

  if ((res = fs_unlink(cstr_make("/loop"))) < 0) {
    panic("fs_setup_mounts: failed to unlink /loop [{:err}]", res);
  }

  DPRINTF("fs_setup_mounts completed successfully\n");
}

//


int fs_register_type(fs_type_t *fs_type) {
  if (hash_map_get(fs_types, fs_type->name) != NULL) {
    EPRINTF("fs type '%s' already registered\n", fs_type->name);
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

//

int fs_mount(cstr_t source, cstr_t mount, const char *fs_type, int flags) {
  fs_type_t *type = hash_map_get(fs_types, fs_type);
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  if (type == NULL) {
    EPRINTF("fs type '%s' not registered\n", fs_type);
    goto_res(ret, -ENODEV);
  }

  // resolve source device
  ventry_t *source_ve = NULL;
  if ((res = vresolve(fs_vcache, at_ve, source, VR_NOFOLLOW|VR_BLK, &source_ve)) < 0) {
    EPRINTF("failed to resolve source path\n");
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
    EPRINTF("failed to resolve mount path\n");
    goto ret;
  }

  // create new vfs and mount it
  vfs_t *vfs = vfs_alloc(type, flags);
  if ((res = vfs_mount(vfs, device, mount_ve)) < 0) {
    EPRINTF("failed to mount fs\n");
    vfs_putref(&vfs);
    goto ret_unlock;
  }
  vfs_putref(&vfs);

LABEL(ret_unlock);
  ve_unlock(mount_ve);
LABEL(ret);
  ve_putref(&mount_ve);
  ve_putref(&at_ve);
  return res;
}

int fs_replace_root(cstr_t new_root) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *newroot_ve = NULL;
  int res;

  if (cstr_eq_charp(new_root, "/")) {
    EPRINTF("new_root cannot be root\n");
    goto_res(ret, -EINVAL);
  }

  // resolve new_root entry
  if ((res = vresolve(fs_vcache, at_ve, new_root, VR_NOFOLLOW|VR_DIR, &newroot_ve)) < 0) {
    EPRINTF("failed to resolve new_root path\n");
    goto ret;
  }
  if (!VE_ISMOUNT(newroot_ve)) {
    EPRINTF("new_root is not a mount point\n");
    ve_unlock(newroot_ve);
    goto_res(ret, -EINVAL);
  }

  // lock the fs root entry
  if (!ve_lock(fs_root_ve)) {
    EPRINTF("fs_root_ve is invalid\n");
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
  ve_putref(&newroot_ve);
  ve_putref(&at_ve);
  return res;
}

int fs_unmount(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *mount_ve = NULL;
  int res;

  // resolve and mount point and lock vfs
  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW|VR_DIR, &mount_ve)) < 0) {
    EPRINTF("failed to resolve mount path\n");
    return res;
  }

  vfs_t *vfs = vfs_getref(VN(mount_ve)->vfs);
  if (!vfs_lock(vfs)) {
    EPRINTF("vfs is dead\n");
    goto_res(ret, -EINVAL);
  }

  // unmount the vfs
  if ((res = vfs_unmount(vfs, mount_ve)) < 0) {
    EPRINTF("failed to unmount fs\n");
  }

  vfs_unlock(vfs);
  vfs_putref(&vfs);

  res = 0; // success
LABEL(ret);
  ve_unlock(mount_ve);
  ve_putref(&mount_ve);
  ve_putref(&at_ve);
  return res;
}

//

int fs_proc_open(proc_t *proc, int fd, cstr_t path, int flags, mode_t mode) {
  ventry_t *at_ve = ve_getref(proc->pwd);
  ventry_t *ve = NULL;
  int res;

  if (fd < 0) {
    // allocate new fd
    fd = ftable_alloc_fd(proc->files, -1);
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

  int vrflags = 0;
  if (flags & O_NOFOLLOW)
    vrflags |= VR_NOFOLLOW;
  if (flags & O_CREAT) {
    vrflags |= VR_PARENT;
    if (flags & O_EXCL)
      vrflags |= VR_EXCLUSV;
  }
  if (flags & O_DIRECTORY) {
    flags &= ~O_TRUNC;
    vrflags |= VR_DIR;
    if (acc != O_RDONLY)
      goto_res(ret, -EINVAL);
    if (flags & O_CREAT)
      goto_res(ret, -EINVAL);
  }

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(path);

  // resolve the path
  res = vresolve_fullpath(fs_vcache, at_ve, path, vrflags, &rpath_buf, &ve);
  if (res < 0 && flags & O_CREAT) {
    // the path does not exist, but we want to create it
    if (ve == NULL)
      goto ret;

    // ve is current set to the locked parent directory
    ventry_t *dve = moveref(ve);
    vnode_t *dvn = VN(dve);
    vn_begin_data_write(dvn);
    res = vn_create(dve, dvn, name, mode, &ve); // create the file entry
    vn_end_data_write(dvn);
    ve_unlock(dve);
    ve_putref(&dve);
    if (res < 0) {
      EPRINTF("failed to create file {:err}\n", res);
      goto ret;
    } else {
      ve_lock(ve); // lock the new entry
    }

    // cache the new entry
    sbuf_write_char(&rpath_buf, '/');
    sbuf_write_cstr(&rpath_buf, name);
    vcache_put(fs_vcache, cstr_from_sbuf(&rpath_buf), ve);
  } else if (res < 0) {
    EPRINTF("failed to resolve path {:err}\n", res);
    goto ret;
  }

  if (vrflags & VR_NOFOLLOW) {
    // check if the file is a symlink or mount
    if (V_ISLNK(ve) || VE_ISMOUNT(ve)) {
      goto_res(ret_unlock, -ELOOP);
    }
  }

  vnode_t *vn = VN(ve);
  file_t *file = f_alloc_vn(flags, vn);
  f_lock(file);

  // open file
  if ((res = f_open(file, flags)) < 0) {
    EPRINTF("failed to open file {:err}\n", res);
    f_unlock_putref(file); // unlock and release the file
    goto ret_unlock;
  }

  // truncate the file if requested and supported
  if ((acc == O_WRONLY || acc == O_RDWR) && (flags & O_TRUNC)) {
    if (F_OPS(file)->f_allocate && (res = F_OPS(file)->f_allocate(file, 0)) < 0) {
      EPRINTF("failed to truncate file {:err}\n", res);
      file->closed = true;
      file->nopen--;
      f_unlock_putref(file); // unlock and release the file
      goto ret_unlock;
    }
  }
  f_unlock(file); // unlock file

  // success
  fd_entry_t *fde = fd_entry_alloc(fd, flags, cstr_from_sbuf(&rpath_buf), f_getref(file));
  ftable_add_entry(proc->files, moveref(fde));

  if (!(flags & O_NOCTTY) && f_isatty(file)) {
    if (proc == curproc) {
      // set it as the controlling terminal
      if ((res = fs_ioctl(fd, TIOCSCTTY, 0)) < 0) {
        EPRINTF("failed to set controlling terminal {:err}\n", res);
        // ignore the error, we can still use the file
      }
    } else {
      DPRINTF("skipping TIOCSCTTY for non-current process\n");
    }
  }

  f_putref(&file); // release reference, fd_entry holds a reference now
  res = fd;
LABEL(ret_unlock);
  if (ve)
    ve_unlock(ve);
LABEL(ret);
  if (res < 0)
    ftable_free_fd(proc->files, fd);

  ve_putref(&ve);
  ve_putref(&at_ve);
  return res;
}

int fs_proc_close(proc_t *proc, int fd) {
  fd_entry_t *fde = ftable_get_remove_entry(proc->files, fd);
  if (fde == NULL)
    return -EBADF;

  int res;
  file_t *file = fde->file;
  if (!f_lock(file)) {
    // file is already closed
    fde_putref(&fde);
    return -EBADF;
  }

  // close the file
  if ((res = f_close(file)) < 0) {
    EPRINTF("failed to close file {:err}\n", res);
    // re-insert the entry back into the ftable
    ftable_add_entry(proc->files, fde_getref(fde));
  } else {
    ftable_free_fd(proc->files, fde->fd);
  }

  f_unlock(file);
  fde_putref(&fde);
  return res;
}

int fs_open(cstr_t path, int flags, mode_t mode) {
  return fs_proc_open(curproc, -1, path, flags, mode);
}

int fs_close(int fd) {
  return fs_proc_close(curproc, fd);
}

vm_file_t *fs_get_vmfile(int fd, size_t off, size_t len) {
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return NULL;

  file_t *file = fde->file;
  if (file->type != FT_VNODE)
    return NULL; // not a vnode file
  if (!f_lock(file))
    return NULL; // file is closed

  vnode_t *vn = file->data;
  vm_file_t *vm_file = vm_file_alloc_vnode(vn_getref(vn), off, len);
  f_unlock(file);
  fde_putref(&fde);
  return vm_file;
}

__ref page_t *fs_getpage(int fd, off_t off) {
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return NULL;

  file_t *file = fde->file;
  // no lock needed for getpage

  int res;
  page_t *outpage = NULL;
  if ((res = F_OPS(file)->f_getpage(file, off, &outpage)) < 0) {
    DPRINTF("failed to get page {:err}\n", res);
    outpage = NULL;
  }

//  f_unlock(file);
  fde_putref(&fde);
  return outpage;
}

__ref page_t *fs_getpage_cow(int fd, off_t off) {
  page_t *page = fs_getpage(fd, off);
  if (page == NULL) {
    EPRINTF("failed to get page\n");
    return NULL;
  }

  // create a copy-on-write page
  page_t *cow_page = alloc_cow_pages(page);
  if (cow_page == NULL) {
    EPRINTF("failed to allocate COW page\n");
    pg_putref(&page); // release the reference
    return NULL;
  }

  pg_putref(&page); // release the original page
  return cow_page;
}

ssize_t fs_kread(int fd, kio_t *kio) {
  ASSERT(kio->dir == KIO_WRITE);
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  ssize_t res;
  file_t *file = fde->file;
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  res = f_read(file, kio);
  if (res < 0) {
//    EPRINTF("failed to read file {:err}\n", res);
  }

  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
  return res;
}

ssize_t fs_kwrite(int fd, kio_t *kio) {
  ASSERT(kio->dir == KIO_READ);
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  ssize_t res;
  file_t *file = fde->file;
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed
  if (file->flags & O_RDONLY)
    goto_res(ret_unlock, -EBADF); // file is not open for writing

  res = f_write(file, kio);
  if (res < 0) {
    EPRINTF("failed to write file {:err}\n", res);
  }

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
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

ssize_t fs_readdir(int fd, void *dirp, size_t len) {
  ssize_t res;
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  file_t *file = fde->file;
  vnode_t *vn = file->data;
  if (!F_ISVNODE(file) || !V_ISDIR(vn))
    goto_res(ret, -ENOTDIR); // not a directory vnode
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
LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
  return res;
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  off_t res;
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  file_t *file = fde->file;
  if (!F_ISVNODE(file))
    goto_res(ret, -EINVAL); // not a vnode file
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  // update the file offset
  vnode_t *vn = file->data;
  switch (whence) {
    case SEEK_SET:
      res = offset;
      break;
    case SEEK_CUR:
      res = file->offset + offset;
      break;
    case SEEK_END:
      res = (off_t) vn->size + offset;
      break;
    default:
      goto_res(ret_unlock, -EINVAL); // invalid whence
  }

  if (res < 0 || res > (off_t) vn->size)
    goto_res(ret_unlock, -EINVAL); // invalid offset

  // update the file offset
  file->offset = res;

LABEL(ret_unlock);
  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
  return res;
}

int fs_ioctl(int fd, unsigned int request, void *argp) {
  DPRINTF("ioctl: fd=%d, request=%#llx, argp=%p\n", fd, request, argp);
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  int res;
  file_t *file = fde->file;
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  res = F_OPS(file)->f_ioctl(file, request, argp);
  if (res < 0) {
    EPRINTF("ioctl failed: fd=%d, request=%#llx, argp=%p, res={:err}\n", fd, request, argp, res);
  }

  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
  DPRINTF("ioctl: fd=%d, request=%#llx, res={:err}\n", fd, request, res);
  return res;
}

int fs_fcntl(int fd, int cmd, unsigned long arg) {
  DPRINTF("fcntl: fd=%d, cmd=%d, arg=%lu\n", fd, cmd, arg);
  int res;
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  switch (cmd) {
    /* duplicate file descriptor */
    case F_DUPFD: {
      int newfd = ftable_alloc_fd(FTABLE, (int)arg);
      if (newfd < 0)
        goto_res(ret, -EMFILE);
      
      fd_entry_t *newfde = fde_dup(fde, newfd);
      newfde->flags &= ~O_CLOEXEC; // not in file table yet, no lock needed
      ftable_add_entry(FTABLE, newfde);
      res = newfd;
      break;
    }
    case F_DUPFD_CLOEXEC: {
      int newfd = ftable_alloc_fd(FTABLE, (int)arg);
      if (newfd < 0)
        goto_res(ret, -EMFILE);
      
      fd_entry_t *newfde = fde_dup(fde, newfd);
      newfde->flags |= O_CLOEXEC; // set close-on-exec flag
      ftable_add_entry(FTABLE, newfde);
      res = newfd;
      break;
    }
    /* get/set file descriptor flags */
    case F_GETFD:
      fde_lock(fde);
      res = (fde->flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
      fde_unlock(fde);
      break;
    case F_SETFD:
      fde_lock(fde);
      if (arg & FD_CLOEXEC)
        fde->flags |= O_CLOEXEC;
      else
        fde->flags &= ~O_CLOEXEC;
      fde_unlock(fde);
      res = 0;
      break;
    /* get/set file status flags */
    case F_GETFL: {
      fde_lock(fde);
      file_t *file = fde->file;
      if (!f_lock(file)) {
        fde_unlock(fde);
        goto_res(ret, -EBADF); // file is closed
      }
      res = file->flags | (fde->flags & O_CLOEXEC);
      f_unlock(file);
      fde_unlock(fde);
      break;
    }
    case F_SETFL: {
      int settable_flags = O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT | O_NOATIME;
      int new_flags = (int)arg & settable_flags;
      
      file_t *file = fde->file;
      if (!f_lock(file))
        goto_res(ret, -EBADF);
      
      file->flags = (file->flags & ~settable_flags) | new_flags;
      f_unlock(file);
      res = 0;
      break;
    }
    /* file/record locking */
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
      EPRINTF("fcntl command %d (file/record locking) not supported\n", cmd);
      goto_res(ret, -ENOSYS);
    /* signal-related commands */
    case F_SETOWN:
    case F_GETOWN:
    case F_SETSIG:
    case F_GETSIG:
      EPRINTF("fcntl command %d (signal-related) not supported\n", cmd);
      goto_res(ret, -ENOSYS);
    default:
      EPRINTF("fcntl invalid command %d\n", cmd);
      goto_res(ret, -EINVAL);
  }

LABEL(ret);
  if (res < 0) {
    EPRINTF("fcntl failed: fd=%d, cmd=%d, arg=%lu, res={:err}\n", fd, cmd, arg, res);
  } else {
    DPRINTF("fcntl: fd=%d, cmd=%d, arg=%lu, res=%d\n", fd, cmd, arg, res);
  }
  fde_putref(&fde);
  return res;
}

int fs_ftruncate(int fd, off_t length) {
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  int res;
  file_t *file = fde->file;
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  res = F_OPS(file)->f_allocate(file, length);
  if (res < 0) {
    DPRINTF("failed to truncate file {:err}\n", res);
  }

LABEL(ret);
  fde_putref(&fde);
  return res;
}

int fs_fstat(int fd, struct stat *stat) {
  int res;
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  file_t *file = fde->file;
  if (!f_lock(file))
    goto_res(ret, -EBADF); // file is closed

  res = F_OPS(file)->f_stat(file, stat);
  f_unlock(file);
LABEL(ret);
  fde_putref(&fde);
  return res;
}

int fs_dup(int fd) {
  int res;
  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  int newfd = ftable_alloc_fd(FTABLE, -1);
  if (newfd < 0)
    goto_res(ret_unlock, -EMFILE);

  fd_entry_t *newfde = fde_dup(fde, newfd);
  mtx_lock(&newfde->lock);
  newfde->flags &= ~O_CLOEXEC; // clear O_CLOEXEC for the new fd
  mtx_unlock(&newfde->lock);
  ftable_add_entry(FTABLE, newfde);

  res = newfd; // success
LABEL(ret_unlock);
  f_unlock(fde->file);
  fde_putref(&fde);
  return res;
}

int fs_dup2(int fd, int newfd) {
  if (fd == newfd)
    return fd;

  fd_entry_t *fde = ftable_get_entry(FTABLE, fd);
  if (fde == NULL)
    return -EBADF;

  int res;
  if (newfd < 0 || newfd >= FTABLE_MAX_FILES)
    goto_res(ret, -EBADF);

  fd_entry_t *existing = ftable_get_remove_entry(FTABLE, newfd);
  if (existing) {
    // close existing file
    file_t *file = existing->file;
    if (f_lock(file)) {
      if ((res = f_close(file)) < 0) {
        EPRINTF("failed to close existing file {:err}\n", res);
      }
      f_unlock(file);
    }
    fde_putref(&existing);
  }

  fd_entry_t *newfde = fde_dup(fde, newfd);
  mtx_lock(&newfde->lock);
  newfde->flags &= ~O_CLOEXEC; // clear O_CLOEXEC for the new fd
  mtx_unlock(&newfde->lock);
  ftable_add_entry(FTABLE, moveref(newfde));

  res = newfd;
LABEL(ret);
  fde_putref(&fde);
  return res;
}

int fs_pipe(int pipefd[2]) {
  return fs_pipe2(pipefd, 0);
}

int fs_pipe2(int pipefd[2], int flags) {
  if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
    return -EINVAL;
  }

  // allocate fs
  int read_fd = ftable_alloc_fd(FTABLE, -1);
  if (read_fd < 0)
    return -EMFILE;
  int write_fd = ftable_alloc_fd(FTABLE, -1);
  if (write_fd < 0) {
    ftable_free_fd(FTABLE, read_fd);
    return -EMFILE;
  }

  // allocate pipe
  pipe_t *pipe = pipe_alloc(PIPE_BUFFER_SIZE);
  if (!pipe) {
    ftable_free_fd(FTABLE, read_fd);
    ftable_free_fd(FTABLE, write_fd);
    return -ENOMEM;
  }

  int res;

  // create the read and write end files
  int read_flags = O_RDONLY | flags;
  file_t *read_file = f_alloc(FT_PIPE, read_flags, pipe_getref(pipe), &pipe_file_ops);

  int write_flags = O_WRONLY | flags;
  file_t *write_file = f_alloc(FT_PIPE, write_flags, pipe_getref(pipe), &pipe_file_ops);

  // open the files
  f_lock(read_file);
  res = f_open(read_file, 0);
  f_unlock(read_file);
  if (res < 0) {
    goto fail;
  }

  f_lock(write_file);
  res = f_open(write_file, 0);
  f_unlock(write_file);
  if (res < 0) {
    f_close(read_file);
    goto fail;
  }

  // add the files to the file table
  fd_entry_t *read_fde = fd_entry_alloc(read_fd, read_flags, cstr_null, moveref(read_file));
  fd_entry_t *write_fde = fd_entry_alloc(write_fd, write_flags, cstr_null, moveref(write_file));
  ftable_add_entry(FTABLE, moveref(read_fde));
  ftable_add_entry(FTABLE, moveref(write_fde));

  // set the pipe file descriptors
  pipefd[0] = read_fd;
  pipefd[1] = write_fd;

  pipe_putref(&pipe);
  return 0;

LABEL(fail);
  pipe_putref(&pipe);
  ftable_free_fd(FTABLE, read_fd);
  ftable_free_fd(FTABLE, write_fd);
  return res;
}

int fs_poll(struct pollfd *fds, size_t nfds, struct timespec *timeout) {
  int res;

  // create a temporary kqueue
  kqueue_t *kq = kqueue_alloc();
  if (kq == NULL) {
    return -ENOMEM;
  }

  // allocate separate arrays for changelist and eventlist
  // worst case: 2 events per fd (read + write)
  struct kevent *changelist = kmallocz(sizeof(struct kevent) * nfds * 2);
  struct kevent *eventlist = kmallocz(sizeof(struct kevent) * nfds * 2);
  if (!changelist || !eventlist) {
    kfree(changelist);
    kfree(eventlist);
    kqueue_free(&kq);
    return -ENOMEM;
  }

  // convert pollfd events to kevents for registration
  int nchanges = 0;
  for (size_t i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0) {
      continue;
    }

    if (fds[i].events & (POLLIN | POLLRDNORM)) {
      DPRINTF("fs_poll: adding fd %d for POLLIN\n", fds[i].fd);
      EV_SET(&changelist[nchanges++], fds[i].fd, EVFILT_READ,
             EV_ADD | EV_ONESHOT, 0, 0, (void *)i);
    }
    if (fds[i].events & (POLLOUT | POLLWRNORM)) {
      DPRINTF("fs_poll: adding fd %d for POLLOUT\n", fds[i].fd);
      EV_SET(&changelist[nchanges++], fds[i].fd, EVFILT_WRITE,
             EV_ADD | EV_ONESHOT, 0, 0, (void *)i);
    }
    if (fds[i].events & POLLPRI) {
      DPRINTF("fs_poll: adding fd %d for POLLPRI\n", fds[i].fd);
      EPRINTF("POLLPRI not supported yet\n");
      // TODO: handle POLLPRI
      todo("fs_poll: handle POLLPRI");
    }
  }

  // register events and wait for ready events
  ssize_t nready = kqueue_wait(kq, changelist, nchanges, eventlist, nfds * 2, timeout);
  if (nready < 0) {
    EPRINTF("kqueue_wait failed: {:err}\n", nready);
    goto_res(ret, (int)nready);
  } else if (nready == 0) {
    goto_res(ret, 0); // no events (timeout)
  }

  // convert kevents back to poll results
  for (ssize_t i = 0; i < nready; i++) {
    size_t idx = (size_t)eventlist[i].udata;
    ASSERT(idx < nfds);

    if (eventlist[i].flags & EV_ERROR) {
      fds[idx].revents |= POLLERR;
      continue;
    }

    switch (eventlist[i].filter) {
      case EVFILT_READ:
        if (eventlist[i].flags & EV_EOF)
          fds[idx].revents |= POLLHUP;
        else
          fds[idx].revents |= POLLIN | POLLRDNORM;
        break;
      case EVFILT_WRITE:
        if (eventlist[i].flags & EV_EOF)
          fds[idx].revents |= POLLHUP;
        else
          fds[idx].revents |= POLLOUT | POLLWRNORM;
        break;
      default:
        EPRINTF("unknown filter %d in kevent\n", eventlist[i].filter);
        break;
    }
  }

  // count how many fds have changed
  int nfds_changed = 0;
  for (size_t i = 0; i < nfds; i++) {
    if (fds[i].revents != 0) {
      nfds_changed = add_checked_overflow(nfds_changed, 1);
    }
  }

  res = nfds_changed; // success
LABEL(ret);
  kqueue_drain(kq);
  kqueue_free(&kq);
  kfree(changelist);
  kfree(eventlist);
  return res;
}

int fs_utimensat(int dirfd, cstr_t filename, struct timespec *utimes, int flags) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(fs_vcache, at_ve, filename, 0, &ve)) < 0)
    goto ret;

  DPRINTF("utimensat: TODO: not implemented yet\n");
  res = 0; // success
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
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
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
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
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
  return res;
}

int fs_create(cstr_t path, mode_t mode) {
  return fs_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int fs_truncate(cstr_t path, off_t length) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  // resolve the path
  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW|VR_DIR, &ve)) < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  }

  vnode_t *vn = VN(ve);
  vn_lock(vn);
  vn_begin_data_write(vn);
  res = vn_fallocate(vn, length); // allocate/truncate the file
  vn_end_data_write(vn);
  vn_unlock(vn);
  if (res < 0) {
    DPRINTF("failed to truncate file\n");
    goto ret_unlock;
  }

  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
  return res;
}

int fs_mknod(cstr_t path, mode_t mode, dev_t dev) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *dve = NULL;
  int res;

  char rpath[PATH_MAX];
  sbuf_t rpath_buf = sbuf_init(rpath, PATH_MAX);
  cstr_t name = cstr_basename(path);

  // resolve the parent directory
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_EXCLUSV|VR_DIR, &rpath_buf, &dve)) < 0)
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

  ve_putref(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_putref(&dve);
  ve_putref(&at_ve);
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
  if ((res = vresolve_fullpath(fs_vcache, at_ve, linkpath, VR_EXCLUSV|VR_DIR, &rpath_buf, &dve)) < 0)
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

  ve_putref(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_putref(&dve);
  ve_putref(&at_ve);
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
  if ((res = vresolve_fullpath(fs_vcache, at_ve, newpath, VR_EXCLUSV|VR_DIR, &rpath_buf, &dve)) < 0)
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

  ve_putref(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ove);
  ve_unlock(dve);
LABEL(ret);
  ve_putref(&ove);
  ve_putref(&dve);
  ve_putref(&at_ve);
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
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_NOTDIR, &rpath_buf, &ve)) < 0)
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
  ve_putref(&ve);
  ve_putref(&dve);
  ve_putref(&at_ve);
  return res;
}

int fs_chdir(cstr_t path) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  int res;

  if ((res = vresolve(fs_vcache, at_ve, path, VR_NOFOLLOW|VR_DIR, &ve)) < 0) {
    DPRINTF("failed to resolve path\n");
    goto ret;
  } else if (ve != at_ve) {
    ve_unlock(ve);
    ve_putref_swap(&curproc->pwd, &ve);
  } else {
    ve_unlock(ve);
  }

  res = 0; // success
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
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
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_EXCLUSV|VR_DIR, &rpath_buf, &dve)) < 0)
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

  ve_putref(&ve);
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(dve);
LABEL(ret);
  ve_putref(&dve);
  ve_putref(&at_ve);
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
  if ((res = vresolve_fullpath(fs_vcache, at_ve, path, VR_DIR, &rpath_buf, &ve)) < 0)
    goto ret;

  vnode_t *vn = VN(ve);
  if (vn->nlink > 2) {
    goto_res(ret_unlock, -ENOTEMPTY);
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
    ve_unlock(dve);
    goto ret_unlock;
  }

  vcache_invalidate(fs_vcache, cstr_from_sbuf(&rpath_buf));
  res = 0; // success
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&dve);
  ve_putref(&at_ve);
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
  ve_putref(&ve);
  ve_putref(&at_ve);
  return res;
}

ssize_t fs_realpath(cstr_t path, kio_t *buf) {
  ventry_t *at_ve = ve_getref(curproc->pwd);
  ventry_t *ve = NULL;
  ssize_t res;

  if ((res = vresolve(fs_vcache, at_ve, path, 0, &ve)) < 0)
    goto ret;

  if (V_ISLNK(ve)) {
    // the real path can be obtained by reading the symlink
    vnode_t *vn = VN(ve);
    vn_begin_data_read(vn);
    res = vn_readlink(vn, buf); // read the link
    vn_end_data_read(vn);
    if (res < 0) {
      DPRINTF("failed to read link\n");
      goto ret_unlock;
    }
  } else {
    char temp[PATH_MAX+1];
    sbuf_t tempbuf = sbuf_init(temp, PATH_MAX+1);
    res = ve_get_path(ve, &tempbuf);
    if (res < 0) {
      DPRINTF("failed to get path\n");
      goto ret_unlock;
    }

    // the real path is the resolved path
    if (sbuf_len(&tempbuf) >= kio_remaining(buf)) {
      DPRINTF("buffer too small for realpath\n");
      res = -ERANGE;
      goto ret_unlock;
    }

    res = (ssize_t) sbuf_transfer_kio(&tempbuf, buf);
  }

  // success
LABEL(ret_unlock);
  ve_unlock(ve);
LABEL(ret);
  ve_putref(&ve);
  ve_putref(&at_ve);
  return res;
}

void fs_print_debug_vcache() {
  vcache_dump(fs_vcache);
}

// MARK: System Calls

DEFINE_SYSCALL(open, int, const char *path, int flags, mode_t mode) {
  DPRINTF("open: path=%s, flags=%#x, mode=%#o\n", path, flags, mode);
  return fs_open(cstr_make(path), flags, mode);
}

SYSCALL_ALIAS(close, fs_close);
SYSCALL_ALIAS(read, fs_read);
SYSCALL_ALIAS(write, fs_write);
SYSCALL_ALIAS(readv, fs_readv);
SYSCALL_ALIAS(writev, fs_writev);
SYSCALL_ALIAS(getdents64, fs_readdir);
SYSCALL_ALIAS(lseek, fs_lseek);
SYSCALL_ALIAS(ioctl, fs_ioctl);
SYSCALL_ALIAS(fcntl, fs_fcntl);
SYSCALL_ALIAS(ftruncate, fs_ftruncate);
SYSCALL_ALIAS(fstat, fs_fstat);
SYSCALL_ALIAS(dup, fs_dup);
SYSCALL_ALIAS(dup2, fs_dup2);
SYSCALL_ALIAS(pipe, fs_pipe);
SYSCALL_ALIAS(pipe2, fs_pipe2);

DEFINE_SYSCALL(poll, int, struct pollfd *fds, nfds_t nfds, int timeout) {
  struct timespec ts;
  struct timespec *tsp = &ts;
  if (timeout > 0) {
    // wait for specified timeout
    ts = timespec_from_nanos(MS_TO_NS(timeout));
  } else if (timeout == 0) {
    // return immediately if no events
    ts = timespec_zero;
  } else {
    // wait indefinitely
    tsp = NULL;
  }

  return fs_poll(fds, nfds, tsp);
}

DEFINE_SYSCALL(utimensat, int, int dfd, const char *filename, struct timespec *utimes, int flags) {
  DPRINTF("utimensat: dfd=%d, filename=%s, utimes=%p, flags=%d\n", dfd, filename, utimes, flags);
  if (vm_validate_ptr((uintptr_t) utimes, /*write=*/true) < 0) {
    return -EFAULT;
  }
  return fs_utimensat(dfd, cstr_make(filename), utimes, flags);
}

DEFINE_SYSCALL(truncate, int, const char *path, off_t length) {
  return fs_truncate(cstr_make(path), length);
}

DEFINE_SYSCALL(stat, int, const char *path, struct stat *stat) {
  return fs_stat(cstr_make(path), stat);
}

DEFINE_SYSCALL(lstat, int, const char *path, struct stat *stat) {
  return fs_lstat(cstr_make(path), stat);
}

DEFINE_SYSCALL(mknod, int, const char *path, mode_t mode, dev_t dev) {
  return fs_mknod(cstr_make(path), mode, dev);
}

DEFINE_SYSCALL(symlink, int, const char *target, const char *linkpath) {
  return fs_symlink(cstr_make(target), cstr_make(linkpath));
}

DEFINE_SYSCALL(link, int, const char *oldpath, const char *newpath) {
  return fs_link(cstr_make(oldpath), cstr_make(newpath));
}

DEFINE_SYSCALL(unlink, int, const char *path) {
  return fs_unlink(cstr_make(path));
}

DEFINE_SYSCALL(chdir, int, const char *path) {
  return fs_chdir(cstr_make(path));
}

DEFINE_SYSCALL(mkdir, int, const char *path, mode_t mode) {
  return fs_mkdir(cstr_make(path), mode);
}

DEFINE_SYSCALL(getcwd, int, char *buf, size_t bufsiz) {
  if (vm_validate_ptr((uintptr_t) buf, /*write=*/true) < 0) {
    return -EFAULT;
  }

  ventry_t *ve = ve_getref(curproc->pwd);
  if (ve == NULL)
    return -ENOENT;

  sbuf_t sbuf = sbuf_init(buf, bufsiz);
  ssize_t res = ve_get_path(ve, &sbuf);
  ve_putref(&ve);

  if (res < 0)
    return -ERANGE;
  return (int) res;
}
