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
#define DPRINTF(fmt, ...) kprintf("vnode: " fmt, ##__VA_ARGS__)
//#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("vnode: %s: " fmt, __func__, ##__VA_ARGS__)

#define CHECK_SAMEDEV(vn1, vn2) if ((vn1)->vfs != (vn2)->vfs) return -EXDEV;
#define CHECK_WRITE(vn) if (VFS_ISRDONLY((vn)->vfs)) return -EROFS;
#define CHECK_DIR(vn) if ((vn)->type != V_DIR) return -ENOTDIR;
#define CHECK_NAMELEN(name) if (cstr_len(name) > NAME_MAX) return -ENAMETOOLONG;
#define CHECK_SUPPORTED(vn, op) if (!(vn)->ops->op) return -ENOTSUP;

static inline mode_t vn_to_mode(vnode_t *vnode) {
  mode_t mode = 0;
  switch (vnode->type) {
    case V_REG: mode |= S_IFREG; break;
    case V_DIR: mode |= S_IFDIR; break;
    case V_LNK: mode |= S_IFLNK; break;
    case V_BLK: mode |= S_IFBLK; break;
    case V_CHR: mode |= S_IFCHR; break;
    case V_FIFO: mode |= S_IFIFO; break;
    case V_SOCK: mode |= S_IFSOCK; break;
    default: panic("invalid vnode type");
  }
  return mode;
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
  ASSERT(vn->state == V_DEAD);
  ASSERT(vn->nopen == 0);
  ASSERT(ref_count(&vn->refcount) == 0);
  if (mtx_owner(&vn->lock) != NULL) {
    ASSERT(mtx_owner(&vn->lock) == curthread);
    mtx_unlock(&vn->lock);
  }

  DPRINTF("!!! vnode cleanup !!! {:+vn}\n", vn);
  if (VN_OPS(vn)->v_cleanup)
    VN_OPS(vn)->v_cleanup(vn);

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
  DPRINTF("vn_read: reading from vnode {:+vn} at offset %ld\n", vn, off);

  if (!VN_OPS(vn)->v_read) return -ENOTSUP;
  if (off < 0) return -EINVAL;
  if (off >= vn->size)
    return 0;

  // filesystem read
  return VN_OPS(vn)->v_read(vn, off, kio);
}

ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  vn_rwlock_assert(vn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_write: writing to vnode {:+vn} at offset %ld\n", vn, off);

  if (VFS_ISRDONLY(vn->vfs)) return -EROFS;
  if (!VN_OPS(vn)->v_write) return -ENOTSUP;
  if (off < 0) return -EINVAL;
  if (off >= vn->size)
    return 0;

  // filesystem write
  return VN_OPS(vn)->v_write(vn, off, kio);
}

int vn_ioctl(vnode_t *vn, unsigned long request, void *arg) {
  vn_lock_assert(vn, LA_OWNED);
  DPRINTF("vn_ioctl: ioctl on vnode {:+vn} with request 0x%lx\n", vn, request);
  DPRINTF("TODO: implement ioct (%s:%d)\n", __FILE__, __LINE__);
  return -EOPNOTSUPP; // not implemented yet
}

int vn_fallocate(vnode_t *vn, off_t length) {
  vn_rwlock_assert(vn, LA_OWNED|LA_XLOCKED);
  DPRINTF("vn_fallocate: allocating space for vnode {:+vn} with length %ld\n", vn, length);

  CHECK_WRITE(vn);
  if (length < 0) return -EINVAL;
  if (!VN_OPS(vn)->v_falloc) return -ENOTSUP;

  // filesystem fallocate
  return VN_OPS(vn)->v_falloc(vn, length - vn->size);
}

void vn_stat(vnode_t *vn, struct stat *statbuf) {
  vn_lock_assert(vn, LA_OWNED);

  memset(statbuf, 0, sizeof(struct stat));
  if (V_ISDEV(vn) && D_OPS(vn->v_dev)->d_stat) {
    D_OPS(vn->v_dev)->d_stat(vn->v_dev, statbuf);
    statbuf->st_mode |= vn_to_mode(vn);
    statbuf->st_dev = make_dev(vn->v_dev);
  } else {
    // vnode stat
    statbuf->st_mode = vn_to_mode(vn);
    statbuf->st_size = (off_t) vn->size;
    statbuf->st_blocks = (blkcnt_t) vn->blocks;
  }

  // vnode only fields
  statbuf->st_ino = vn->id;
  statbuf->st_nlink = vn->nlink;
  statbuf->st_rdev = vn->device ? make_dev(vn->device) : 0;

  // TODO: rest of the fields
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
  DPRINTF("vn_readlink: reading link from vnode {:+vn}\n", vn);

  CHECK_SUPPORTED(vn, v_readlink);
  int res;

  if (str_len(vn->v_link) == 0) {
    // read link from filesystem
    str_t link = str_alloc_empty(vn->size);
    kio_t lnkio = kio_writeonly_from_str(link);
    if ((res = VN_OPS(vn)->v_readlink(vn, &lnkio)) < 0) {
      str_free(&link);
      return res;
    }

    // save link
    vn->v_link = link;
  }

  // copy link to kio
  kio_t lnkio = kio_readonly_from_str(vn->v_link);
  kio_transfer(kio, &lnkio);
  return 0;
}

ssize_t vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  vn_rwlock_assert(vn, LA_OWNED|LA_SLOCKED);
  DPRINTF("vn_readdir: reading directory {:+vn} at offset %ld\n", vn, off);

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
      // TODO: revalidate ventry
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
  ve_add_child(dve, ve);
  vfs_add_node(vfs, ve);
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
  ASSERT(F_ISVNODE(file));
  if (file->nopen > 0) {
    // just increment the open count
    DPRINTF("f_open: incrementing count for file %p\n", file);
    file->nopen++;
    return 0;
  }

  DPRINTF("f_open: opening file %p with flags 0x%x\n", file, flags);
  int res;
  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device open
    res = d_open(device, flags);
  } else {
    // vnode open
    res = vn_open(vn, flags);
  }

  if (res >= 0) {
    file->nopen++;
  } else {
    EPRINTF("failed to open vnode {:err}\n", res);
  }

  vn_unlock(vn);
  return 0;
}

int vn_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  if (file->nopen > 1) {
    // just decrement the open count
    DPRINTF("f_close: decrementing count for file %p\n", file);
    file->nopen--;
    return 0;
  }

  int res;
  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  DPRINTF("f_close: closing file %p\n", file);
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device close
    res = d_close(device);
  } else {
    // vnode close
    res = vn_close(vn);
  }

  if (res >= 0) {
    file->nopen--;
    file->closed = true;
  } else {
    EPRINTF("failed to close vnode {:err}\n", res);
  }

  vn_unlock(vn);
  return res;
}

int vn_f_allocate(file_t *file, off_t len) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));

  vnode_t *vn = file->data;
  if (V_ISDEV(vn)) {
    // ignored for devices
    return 0;
  } else if (!((file->flags == O_WRONLY || file->flags == O_RDWR))) {
    // file must be opened for writing
    return -EBADF; // bad file descriptor
  }

  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  int res;
  vn_begin_data_write(vn);
  res = vn_fallocate(vn, len);
  vn_end_data_write(vn);
  if (res < 0) {
    EPRINTF("failed to truncate vnode {:err}\n", res);
  }
  vn_unlock(vn);
  return res;
}

int vn_f_getpage(file_t *file, off_t off, __move page_t **page) {
  // file does not need to be locked
  ASSERT(F_ISVNODE(file));

  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  int res;
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device getpage
    page_t *out = d_getpage(device, off);
    if (out == NULL) {
      res = -EIO; // device failed to get page
    } else {
      *page = moveref(out); // move the page to caller
      res = 0; // success
    }
  } else {
    // getpage from vnode
    res = vn_getpage(vn, off, /*cached=*/true, page);
  }

  vn_unlock(vn);
  return res;
}

ssize_t vn_f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));

  if (file->flags & O_WRONLY)
    return -EBADF; // file is not open for reading

  ssize_t res;
  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // this operation can block so we unlock the file during the read
  f_unlock(file);
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device read
    res = d_read(device, file->offset, kio);
  } else {
    // read the file
    vn_begin_data_read(vn);
    res = vn_read(vn, file->offset, kio);
    vn_end_data_read(vn);
  }
  // and re-lock the file
  f_lock(file);

  if (res > 0) {
    // update the file offset
    file->offset += res;
  }

  vn_unlock(vn);
  return res;
}

ssize_t vn_f_write(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(F_ISVNODE(file));
  if (file->flags & O_RDONLY)
    return -EBADF; // file is not open for writing

  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (file->flags & O_RDONLY) {
    return -EBADF; // file is not open for writing
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // this operation can block so we unlock the file during the write
  f_unlock(file);
  ssize_t res;
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device write
    res = d_write(device, file->offset, kio);
  } else {
    // write the file
    vn_begin_data_write(vn);
    res = vn_write(vn, file->offset, kio);
    vn_end_data_write(vn);
  }
  // and re-lock the file
  f_lock(file);

  if (res > 0) {
    // update the file offset
    file->offset += res;
  }

  vn_unlock(vn);
  return res;
}

int vn_f_ioctl(file_t *file, unsigned long request, void *arg) {
  ASSERT(F_ISVNODE(file));
  f_lock_assert(file, LA_OWNED);

  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  int res;
  if (V_ISDEV(vn)) {
    // device ioctl
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);
    res = d_ioctl(device, request, arg);
  } else {
    // vnode ioctl
    res = vn_ioctl(vn, request, arg);
  }

  if (res == -ENOTSUP)
    res = -ENOTTY; // not a tty device or not supported

  vn_unlock(vn);
  return res;
}

int vn_f_stat(file_t *file, struct stat *statbuf) {
  ASSERT(F_ISVNODE(file));
  f_lock_assert(file, LA_OWNED);

  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // get the vnode stat
  vn_stat(vn, statbuf);
  vn_unlock(vn);
  return 0;
}

int vn_f_kqevent(file_t *file, knote_t *kn) {
  DPRINTF("vn_f_kqevent called for file %p [%s]\n", file, evfilt_to_string(kn->event.filter));

  // called from the file `event` filter_ops method
  vnode_t *vn = file->data;
  ASSERT(vn != NULL);
  ASSERT(kn->event.filter == EVFILT_READ);

  if (!vn_lock(vn)) {
    kn->event.flags |= EV_EOF;
    return 1; // vnode is dead, report EOF
  }

  int event;
  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    if (D_OPS(device)->d_kqevent) {
      event = D_OPS(device)->d_kqevent(device, kn);
      DPRINTF("vn_f_kqevent: device %d kqevent returned %d\n", make_dev(device), event);
    } else {
      DPRINTF("vn_f_kqevent: device %d does not support kqevent\n", make_dev(device));
      event = 0;
    }
  } else if (V_ISREG(vn)) {
    // regular file handling EVFILT_READ
    size_t f_off = kn->fde->file->offset;
    size_t vn_size = vn->size;

    // we report data as readable if the file offset is less than the size
    // of the vnode, and return the number of bytes available to read
    if (f_off < vn_size) {
      kn->event.data = (intptr_t)(vn_size - f_off);
      event = 1;
    } else {
      event = 0;
    }
    DPRINTF("vn_f_kqevent: regular file {:+vn} kqevent returned %d with data %ld\n", vn, event, kn->event.data);
  } else {
    todo("vn_f_kqevent: implement kqevent for {:vt}", vn->type);
  }

  vn_unlock(vn);
  if (event < 0) {
    EPRINTF("failed to get kqevent for vnode {:err}\n", event);
  } else if (event == 0) {
    kn->event.data = 0;
    DPRINTF("no data available for vnode {:+vn}\n", vn);
  } else {
    DPRINTF("data available for vnode {:+vn}: %lld bytes\n", vn, kn->event.data);
  }
  return event;
}

void vn_f_cleanup(file_t *file) {
  ASSERT(F_ISVNODE(file));
  if (mtx_owner(&file->lock) != NULL) {
    ASSERT(mtx_owner(&file->lock) == curthread);
  }

  vnode_t *vn = moveptr(file->data);
  vn_putref(&vn);
}


struct file_ops vnode_file_ops = {
  .f_open = vn_f_open,
  .f_close = vn_f_close,
  .f_allocate = vn_f_allocate,
  .f_getpage = vn_f_getpage,
  .f_read = vn_f_read,
  .f_write = vn_f_write,
  .f_ioctl = vn_f_ioctl,
  .f_stat = vn_f_stat,
  .f_kqevent = vn_f_kqevent,
  .f_cleanup = vn_f_cleanup,
};
