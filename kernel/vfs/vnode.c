//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/file.h>

#include <kernel/proc.h>
#include <kernel/mm.h>
#include <kernel/kevent.h>
#include <kernel/printf.h>

#include <abi/termios.h>

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("vnode: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("vnode: %s: " fmt, __func__, ##__VA_ARGS__)

#define CHECK_SAMEDEV(vn1, vn2) if ((vn1)->vfs != (vn2)->vfs) return -EXDEV;
#define CHECK_WRITE(vn) if (VFS_ISRDONLY((vn)->vfs)) return -EROFS;
#define CHECK_DIR(vn) if ((vn)->type != V_DIR) return -ENOTDIR;
#define CHECK_NAMELEN(name) if (cstr_len(name) > NAME_MAX) return -ENAMETOOLONG;
#define CHECK_SUPPORTED(vn, op) if (!(vn)->ops->op) return -ENOTSUP;

static inline mode_t vn_to_mode(vnode_t *vnode) {
  return vnode->mode;
}

//
// MARK: Vnode API
//

__ref vnode_t *vn_alloc_empty(enum vtype type) {
  vnode_t *vnode = kmallocz(sizeof(vnode_t));
  vnode->id = 0;
  vnode->type = type;
  vnode->state = V_EMPTY;
  vnode->flags = 0;
  mtx_init(&vnode->lock, MTX_RECURSIVE, "vnode_lock");
  rw_init(&vnode->data_lock, 0, "vnode_data_lock");
  ref_init(&vnode->refcount);
  knlist_init(&vnode->knlist, &vnode->lock.lo);

  VN_DPRINTF("ref init {:+vn} [1]", vnode);
  DPRINTF("allocated {:+vn}\n", vnode);
  return vnode;
}

__ref vnode_t *vn_alloc(id_t id, struct vattr *vattr) {
  vnode_t *vnode = vn_alloc_empty(vattr->type);
  vnode->id = id;
  vnode->mode = vattr->mode;
  return vnode;
}

__ref struct pgcache *vn_get_pgcache(vnode_t *vn) {
  if (!vn_lock(vn)) {
    panic("vnode is dead");
  }

  if (vn->pgcache == NULL) {
    uint16_t order = pgcache_size_to_order(page_align(vn->size), PAGE_SIZE);
    vn->pgcache = pgcache_alloc(order, PAGE_SIZE);
  }

  struct pgcache *pgcache = getref(vn->pgcache);
  vn_unlock(vn);
  return pgcache;
}

bool vn_isatty(vnode_t *vn) {
  vn_lock_assert(vn, LA_OWNED);
  if (!V_ISDEV(vn)) {
    return false; // not a character or block device
  }

  // try sending a TIOCGWINSZ ioctl to the device
  device_t *device = vn->v_dev;
  struct winsize ws;
  int res = d_ioctl(device, TIOCGWINSZ, &ws);
  if (res < 0) {
    // is not a tty device
    return false;
  }
  // is a tty device
  return true;
}

void _vn_cleanup(__move vnode_t **vnref) {
  // called when last reference is released
  vnode_t *vn = moveref(*vnref);
  ASSERT(vn->state == V_ALIVE || vn->state == V_DEAD);
  ASSERT(vn->nopen == 0);
  ASSERT(ref_count(&vn->refcount) == 0);
  if (mtx_owner(&vn->lock) != NULL) {
    ASSERT(mtx_owner(&vn->lock) == curthread);
    mtx_unlock(&vn->lock);
  }

  // a vnode may be cleaned up when in a state other than V_DEAD. for example, when
  // a vnode is allocated and linked with a ventry marked with the VE_NOSAVE flag,
  // the only long-lived reference to the vnode lives in the file that opened it.
  // when this file is closed, the vnode is cleaned up. this is used when for
  // filesystems with transient files (e.g. procfs).

  DPRINTF("!!! vnode cleanup !!! {:+vn}\n", vn);
  if (VN_OPS(vn)->v_cleanup)
    VN_OPS(vn)->v_cleanup(vn);

  ASSERT(vn->data == NULL);
  vfs_putref(&vn->vfs);
  mtx_destroy(&vn->lock);
  rw_destroy(&vn->data_lock);
  kfree(vn);
}

//

int vn_open(vnode_t *vn, int flags) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_open: opening vnode {:+vn} with flags 0x%x\n", vn, flags);

  // only call filesystem open on the first open
  int res;
  if (vn->nopen == 0) {
    // filesystem open
    if (VN_OPS(vn)->v_open && (res = VN_OPS(vn)->v_open(vn, flags)) < 0) {
      return res;
    } else {
      vn->flags |= VN_OPEN; // set open flag
    }
  }

  // increment open count
  vn->nopen++;

  return 0;
}

int vn_close(vnode_t *vn) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_close: closing vnode {:+vn}\n", vn);

  // only call filesystem close when the open count reaches zero
  int res;
  if (vn->nopen == 1) {
    // filesystem close
    if (VN_OPS(vn)->v_close && (res = VN_OPS(vn)->v_close(vn)) < 0) {
      return res;
    } else {
      vn->flags &= ~VN_OPEN; // clear open flag
    }
  }

  // decrement open count
  vn->nopen--;

  return 0;
}

int vn_getpage(vnode_t *vn, off_t off, bool cached, __move page_t **result) {
  if (!VN_OPS(vn)->v_getpage) return -ENOTSUP;
  if (off < 0) return -EINVAL;

  page_t *page;
  if (cached && vn->pgcache && (page = pgcache_lookup(vn->pgcache, off)) != NULL) {
    *result = page;
    return 0;
  }

  int res;
  if (V_ISDEV(vn)) {
    if (!D_OPS(vn->v_dev)->d_getpage) {
      return -ENOTSUP; // device does not support getpage
    }

    // device getpage
    page = D_OPS(vn->v_dev)->d_getpage(vn->v_dev, off);
    res = page ? 0 : -EIO;
  } else {
    // filesystem getpage
    res = VN_OPS(vn)->v_getpage(vn, off, &page);
  }

  if (res < 0) {
    return res;
  }

  *result = page;
  return 0;
}

ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  vn_rwlock_assert(vn, LA_OWNED|LA_SLOCKED);
//  DPRINTF("vn_read: reading from vnode {:+vn} at offset %lld\n", vn, off);

  if (!VN_OPS(vn)->v_read) return -ENOTSUP;
  if (off < 0) return -EINVAL;

  // filesystem read
  return VN_OPS(vn)->v_read(vn, off, kio);
}

ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  vn_rwlock_assert(vn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_write: writing to vnode {:+vn} at offset %lld\n", vn, off);

  if (VFS_ISRDONLY(vn->vfs)) return -EROFS;
  if (!VN_OPS(vn)->v_write) return -ENOTSUP;
  if (off < 0) return -EINVAL;

  // filesystem write
  return VN_OPS(vn)->v_write(vn, off, kio);
}

int vn_ioctl(vnode_t *vn, unsigned long request, void *arg) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_ioctl: ioctl on vnode {:+vn} with request 0x%llx\n", vn, request);
  DPRINTF("TODO: implement ioct (%s:%d)\n", __FILE__, __LINE__);
  return -EOPNOTSUPP; // not implemented yet
}

int vn_fallocate(vnode_t *vn, off_t length) {
  vn_rwlock_assert(vn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_fallocate: allocating space for vnode {:+vn} with length %lld\n", vn, length);

  CHECK_WRITE(vn);
  if (length < 0) return -EINVAL;
  if (!VN_OPS(vn)->v_falloc) return -ENOTSUP;

  // filesystem fallocate
  return VN_OPS(vn)->v_falloc(vn, length - vn->size);
}

void vn_stat(vnode_t *vn, struct stat *statbuf) {
  vn_lock_assert(vn, LA_OWNED);

  memset(statbuf, 0, sizeof(struct stat));
  statbuf->st_dev = make_dev(vn->v_dev);
  statbuf->st_mode = vn_to_mode(vn);
  statbuf->st_size = (off_t) vn->size;
  statbuf->st_blocks = (blkcnt_t) vn->blocks;

  // vnode only fields
  statbuf->st_ino = vn->id;
  statbuf->st_nlink = vn->nlink;
  statbuf->st_rdev = vn->device ? make_dev(vn->device) : 0;

  // timestamps
  statbuf->st_atim.tv_sec = vn->atime;
  statbuf->st_mtim.tv_sec = vn->mtime;
  statbuf->st_ctim.tv_sec = vn->ctime;
}

//

int vn_load(vnode_t *vn) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_load: loading vnode {:+vn}\n", vn);

  if (VN_ISLOADED(vn)) return 0;
  if (!VN_OPS(vn)->v_load) return 0;
  int res;

  if ((res = VN_OPS(vn)->v_load(vn)) < 0) {
    return res;
  }

  vn->flags |= VN_LOADED;
  return 0;
}

int vn_save(vnode_t *vn) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_save: saving vnode {:+vn}\n", vn);

  CHECK_WRITE(vn);
  if (!VN_ISDIRTY(vn)) return 0;
  if (!VN_OPS(vn)->v_save) return 0;
  int res;

  if ((res = VN_OPS(vn)->v_save(vn)) < 0) {
    return res;
  }

  vn->flags &= ~VN_DIRTY;
  return res;
}

int vn_readlink(vnode_t *vn, kio_t *kio) {
  vn_rwlock_assert(vn, LA_OWNED|LA_SLOCKED);
  CHECK_SUPPORTED(vn, v_readlink);
  DPRINTF("vn_readlink: reading link from vnode {:+vn}\n", vn);

  int res = 0;
  if (!str_isnull(vn->v_link)) {
    // copy link to kio
    kio_t lnkio = kio_readonly_from_str(vn->v_link);
    res = (int) kio_transfer(kio, &lnkio);
  } else {
    if ((res = VN_OPS(vn)->v_readlink(vn, kio)) < 0) {
      return res;
    }
  }

  return res;
}

ssize_t vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  vn_rwlock_assert(vn, LA_OWNED|LA_SLOCKED);
  DPRINTF("vn_readdir: reading directory {:+vn} at offset %lld\n", vn, off);

  CHECK_DIR(vn);
  CHECK_SUPPORTED(vn, v_readdir);
  if (off < 0) return -EINVAL;
  return VN_OPS(vn)->v_readdir(vn, off, dirbuf);
}

//

int vn_lookup(ventry_t *dve, vnode_t *dvn, cstr_t name, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_SLOCKED);
  DPRINTF("vn_lookup: looking up name '{:cstr}' in directory {:+vn}\n", &name, dvn);

  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // check already loaded children
  LIST_FOR_IN(child, &dve->children, list) {
    if (ve_cmp_cstr(child, name)) {
      if (!ve_validate(child)) {
        ve_remove_child(dve, child);
        continue;
      }

      *result = ve_getref(child); // move new ref to caller
      return 0;
    }
  }

  // READ BEGIN
  if (!vfs_begin_read_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem lookup
  if ((res = VN_OPS(dvn)->v_lookup(dvn, name, &ve)) < 0) {
    vfs_end_read_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  if (VE_ISNOSAVE(ve)) {
    vfs_activate_node(vfs, ve);
  } else {
    ve_add_child(dve, ve);
    vfs_add_node(vfs, ve);
  }
  vfs_end_read_op(vfs);
  // READ END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_create(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_create: creating file '{:cstr}' in directory {:+vn} with mode 0x%x\n", &name, dvn, mode);

  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_create);
  struct vattr attr = make_vattr(V_REG, mode);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem create
  if ((res = VN_OPS(dvn)->v_create(dvn, name, &attr, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  ve_add_child(dve, ve);
  vfs_add_node(vfs, ve);
  vfs_end_write_op(vfs);
  // WRITE END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_mknod(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, dev_t dev, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_mknod: creating node '{:cstr}' in directory {:+vn} with mode 0x%x and dev 0x%x\n", &name, dvn, mode, dev);

  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_mknod);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  struct vattr attr;
  if (mode & S_IFBLK) {
    attr = make_vattr(V_BLK, mode);
  } else if (mode & S_IFCHR) {
    attr = make_vattr(V_CHR, mode);
  } else {
    return -EINVAL;
  }

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem mknod
  if ((res = VN_OPS(dvn)->v_mknod(dvn, name, &attr, dev, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vnode_t *vn = VN(ve);
  vn->v_dev = device_get(dev);
  ve_add_child(dve, ve);
  vfs_add_node(vfs, ve);
  vfs_end_write_op(vfs);
  // WRITE END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_symlink(ventry_t *dve, vnode_t *dvn, cstr_t name, cstr_t target, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_symlink: creating symlink '{:cstr}' in directory {:+vn} with target '{:cstr}'\n", &name, dvn, &target);

  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_NAMELEN(target);
  CHECK_SUPPORTED(dvn, v_symlink);
  struct vattr attr = make_vattr(V_LNK, S_IFLNK);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem symlink
  if ((res = VN_OPS(dvn)->v_symlink(dvn, name, &attr, target, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  ve_add_child(dve, ve);
  vfs_add_node(vfs, ve);
  vfs_end_write_op(vfs);
  // WRITE END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_hardlink(ventry_t *dve, vnode_t *dvn, cstr_t name, vnode_t *target, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  vn_lock_assert(target, LA_OWNED);
  DPRINTF("vn_hardlink: creating hardlink '{:cstr}' in directory {:+vn} to target {:+vn}\n", &name, dvn, target);

  CHECK_SAMEDEV(dvn, target);
  CHECK_WRITE(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_hardlink);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem hardlink
  if ((res = VN_OPS(dvn)->v_hardlink(dvn, name, target, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  ve_add_child(dve, ve);
  vfs_end_write_op(vfs);
  // WRITE END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_unlink(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  ve_lock_assert(ve, LA_OWNED);
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_unlink: unlinking vnode {:+ve} from directory {:+dve} and vnode {:+vn}\n", ve, dve, vn);

  CHECK_SAMEDEV(dvn, vn);
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_SUPPORTED(dvn, v_unlink);
  vfs_t *vfs = dvn->vfs;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem unlink
  if ((res = VN_OPS(dvn)->v_unlink(dvn, vn, ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  if (vn->nlink == 1) {
    vfs_remove_node(vfs, vn); // this marks the vnode dead
    ve_syncvn(ve); // this marks the ventry dead
  }
  ve_remove_child(dve, ve);
  ve_unlink_vnode(ve, vn);
  vfs_end_write_op(vfs);
  // WRITE END

  return 0;
}

int vn_mkdir(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_mkdir: creating directory '{:cstr}' in directory {:+vn} with mode 0x%x\n", &name, dvn, mode);

  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_mkdir);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem mkdir
  struct vattr attr = make_vattr(V_DIR, mode);
  if ((res = VN_OPS(dvn)->v_mkdir(dvn, name, &attr, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  ve_add_child(dve, ve);
  vfs_add_node(vfs, ve);
  vfs_end_write_op(vfs);
  // WRITE END

  if (result)
    *result = moveref(ve);
  else
    ve_putref(&ve);
  return 0;
}

int vn_rmdir(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
  ve_lock_assert(dve, LA_OWNED);
  vn_rwlock_assert(dvn, LA_OWNED|LA_XLOCKED);
  ve_lock_assert(ve, LA_OWNED);
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_rmdir: removing directory {:+ve} from directory {:+dve} and vnode {:+vn}\n", ve, dve, vn);

  CHECK_DIR(vn);
  CHECK_WRITE(dvn);
  CHECK_SUPPORTED(dvn, v_rmdir);
  vfs_t *vfs = dvn->vfs;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem rmdir
  if ((res = VN_OPS(dvn)->v_rmdir(dvn, vn, ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  ve_remove_child(dve, ve);
  vfs_remove_node(vfs, vn); // this marks the vnode dead
  ve_syncvn(ve); // this marks the ventry dead

  ve_unlink_vnode(ve, vn);
  vfs_end_write_op(vfs);
  // WRITE END

  return 0;
}

//
// MARK: Vnode File Operations
//

int vn_f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 0);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  DPRINTF("vn_f_open: opening file %p with flags 0x%x [vn {:+vn}]\n", file, flags, vn);
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // vnode open
  int res = vn_open(vn, flags);
  if (res < 0) {
    EPRINTF("failed to open file %p [vn {:+vn}] {:err}\n", file, vn, res);
  }

  vn_unlock(vn);
  return res;
}

int vn_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen == 1);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  DPRINTF("vn_f_close: closing file %p [vn {:+vn}]\n", file, vn);
  
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // vnode close
  int res = vn_close(vn);
  if (res < 0) {
    EPRINTF("failed to close file %p [vn {:+vn}] {:err}\n", file, vn, res);
  }

  vn_unlock(vn);
  return res;
}

int vn_f_allocate(file_t *file, off_t len) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));
  int flags = file->flags & O_ACCMODE;
  if (!((flags == O_WRONLY || flags == O_RDWR))) {
    EPRINTF("vn_f_allocate: file %p not opened for writing (flags=0x%x)\n", file, flags);
    return -EBADF; // file must be opened for writing
  }

  vnode_t *vn = file->data;
  DPRINTF("vn_f_allocate: allocating space for file %p with length %lld [vn {:+vn}]\n", file, len, vn);
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // vnode fallocate
  vn_begin_data_write(vn);
  int res = vn_fallocate(vn, len);
  vn_end_data_write(vn);
  if (res < 0) {
    EPRINTF("failed to allocate space for file %p [vn {:+vn}] {:err}\n", file, vn, res);
  }

  vn_unlock(vn);
  return res;
}

int vn_f_getpage(file_t *file, off_t off, __move page_t **page) {
  // file does not need to be locked
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
//  DPRINTF("vn_f_getpage: getting page for file %p at offset %lld [vn {:+vn}]\n", file, off, vn);
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // vnode getpage
  int res = vn_getpage(vn, off, /*cached=*/true, page);
  if (res < 0) {
    EPRINTF("failed to get page for file %p at offset %lld [vn {:+vn}] {:err}\n", file, off, vn, res);
  }

  vn_unlock(vn);
  return res;
}

ssize_t vn_f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  //DPRINTF("vn_f_read: reading from file %p at offset %lld [vn {:+vn}]\n", file, file->offset, vn);

  // this operation can block so we unlock the file during the read
  f_unlock(file);
  // vnode read
  vn_begin_data_read(vn);
  ssize_t res = vn_read(vn, file->offset, kio);
  vn_end_data_read(vn);
  // and re-lock the file
  f_lock(file);

  if (res < 0) {
    EPRINTF("failed to read from file %p at offset %lld [vn {:+vn}] {:err}\n", file, file->offset, vn, (int)res);
  }

  vn_unlock(vn);
  if (res > 0) {
    // update the file offset
    file->offset += res;
  }
  return res;
}

ssize_t vn_f_write(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  DPRINTF("vn_f_write: writing to file %p at offset %lld [vn {:+vn}]\n", file, file->offset, vn);
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // this operation can block so we unlock the file during the write
  f_unlock(file);
  // vnode write
  vn_begin_data_write(vn);
  ssize_t res = vn_write(vn, file->offset, kio);
  vn_end_data_write(vn);
  // and re-lock the file
  f_lock(file);

  if (res < 0) {
    EPRINTF("failed to write to file %p at offset %lld [vn {:+vn}] {:err}\n", file, file->offset, vn, (int)res);
  }

  vn_unlock(vn);
  if (res > 0) {
    // update the file offset
    file->offset += res;
  }
  return res;
}

ssize_t vn_f_readdir(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  if (!V_ISDIR(vn)) {
    return -ENOTDIR; // file is not a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  DPRINTF("vn_f_readdir: reading directory from file %p at offset %lld [vn {:+vn}]\n", file, file->offset, vn);
  // this operation can block so we unlock the file during the read
  f_unlock(file);
  // read the directory
  vn_begin_data_read(vn);
  ssize_t res = vn_readdir(vn, file->offset, kio);
  vn_end_data_read(vn);
  // and re-lock the file
  f_lock(file);

  vn_unlock(vn);
  if (res < 0) {
    EPRINTF("failed to read directory from file %p at offset %lld [vn {:+vn}] {:err}\n", file, file->offset, vn, (int)res);
    return res;
  }

  // update the file offset
  file->offset += res;
  return (ssize_t) kio_transfered(kio);
}

off_t vn_f_lseek(file_t *file, off_t offset, int whence) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  DPRINTF("vn_f_lseek: seeking file %p from offset %lld with whence %d [vn {:+vn}]\n", file, offset, whence, vn);
  off_t newoff;
  switch (whence) {
    case SEEK_SET:
      newoff = offset;
      break;
    case SEEK_CUR:
      newoff = file->offset + offset;
      break;
    case SEEK_END:
      newoff = (off_t)vn->size + offset;
      break;
    default:
      vn_unlock(vn);
      return -EINVAL; // invalid whence
  }

  if (newoff < 0) {
    vn_unlock(vn);
    return -EINVAL; // invalid offset
  }

  file->offset = newoff;
  vn_unlock(vn);
  return newoff;
}

int vn_f_stat(file_t *file, struct stat *statbuf) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  DPRINTF("vn_f_stat: getting stat for file %p [vn {:+vn}]\n", file, vn);
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // get the vnode stat
  vn_stat(vn, statbuf);
  vn_unlock(vn);
  return 0;
}

int vn_f_ioctl(file_t *file, unsigned int request, void *arg) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));

  vnode_t *vn = file->data;
  DPRINTF("vn_f_ioctl: ioctl on file %p with request %#llx [vn {:+vn}]\n", file, request, vn);
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // vnode ioctl
  int res = vn_ioctl(vn, request, arg);
  if (res == -ENOTSUP)
    res = -ENOTTY; // not a tty device or not supported
  else if (res < 0)
    EPRINTF("failed to ioctl file %p [vn {:+vn}] {:err}\n", file, vn, res);

  vn_unlock(vn);
  return res;
}

int vn_f_kqevent(file_t *file, knote_t *kn) {
  // file does not need to be locked
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));
  
  // called from the file `event` filter_ops method
  vnode_t *vn = file->data;
  DPRINTF("vn_f_kqevent: checking kqevent for file %p [vn {:+vn}, filter %s]\n", 
          file, vn, evfilt_to_string(kn->event.filter));
  ASSERT(kn->event.filter == EVFILT_READ);

  if (!vn_lock(vn)) {
    kn->event.flags |= EV_EOF;
    return 1; // vnode is dead, report EOF
  }

  int res;
  if (V_ISREG(vn)) {
    // regular file handling EVFILT_READ
    size_t f_off = kn->fde->file->offset;
    size_t vn_size = vn->size;

    // we report data as readable if the file offset is less than the size
    // of the vnode, and return the number of bytes available to read
    if (f_off < vn_size) {
      kn->event.data = (intptr_t)(vn_size - f_off);
      res = 1;
    } else {
      res = 0;
    }
    DPRINTF("vn_f_kqevent: regular file {:+vn} kqevent returned %d with data %lld\n", vn, res, kn->event.data);
  } else {
    todo("vn_f_kqevent: implement kqevent for {:vt}", vn->type);
  }

  vn_unlock(vn);
  if (res < 0) {
    EPRINTF("failed to get kqevent for file %p [vn {:+vn}] {:err}\n", file, vn, res);
  } else if (res == 0) {
    kn->event.data = 0;
    DPRINTF("vn_f_kqevent: no data available for file %p [vn {:+vn}]\n", file, vn);
  } else {
    DPRINTF("vn_f_kqevent: %lld bytes available for file %p [vn {:+vn}]\n", kn->event.data, file, vn);
  }
  return res;
}

void vn_f_cleanup(file_t *file) {
  ASSERT(F_ISVNODE(file));
  ASSERT(!V_ISDEV((vnode_t *)file->data));
  if (mtx_owner(&file->lock) != NULL) {
    ASSERT(mtx_owner(&file->lock) == curthread);
  }

  vnode_t *vn = moveptr(file->data);
  vn_putref(&vn);
}

// referenced in file.c
struct file_ops vnode_file_ops = {
  .f_open = vn_f_open,
  .f_close = vn_f_close,
  .f_allocate = vn_f_allocate,
  .f_getpage = vn_f_getpage,
  .f_read = vn_f_read,
  .f_readdir = vn_f_readdir,
  .f_write = vn_f_write,
  .f_lseek = vn_f_lseek,
  .f_stat = vn_f_stat,
  .f_ioctl = vn_f_ioctl,
  .f_kqevent = vn_f_kqevent,
  .f_cleanup = vn_f_cleanup,
};
