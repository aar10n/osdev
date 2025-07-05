//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/file.h>

#include <kernel/mm.h>
#include <kernel/printf.h>

#include <abi/termios.h>

#define ASSERT(x) kassert(x)
// #define DPRINTF(fmt, ...) kprintf("vnode: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("vnode: %s: " fmt, __func__, ##__VA_ARGS__)

#define CHECK_SAMEDEV(vn1, vn2) if ((vn1)->vfs != (vn2)->vfs) return -EXDEV;
#define CHECK_WRITE(vn) if (VFS_ISRDONLY((vn)->vfs)) return -EROFS;
#define CHECK_DIR(vn) if ((vn)->type != V_DIR) return -ENOTDIR;
#define CHECK_NAMELEN(name) if (cstr_len(name) > NAME_MAX) return -ENAMETOOLONG;
#define CHECK_SUPPORTED(vn, op) if (!(vn)->ops->op) return -ENOTSUP;

#define goto_res(lbl, err) do { res = err; goto lbl; } while (0)

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
// MARK: Vnode File Operations
//

int vn_f_close(file_t *file) {
  int res;
  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  if (V_ISDEV(vn)) {
    device_t *device = vn->v_dev;
    ASSERT(device != NULL);

    // device close
    res = d_close(device);
  } else {
    // close the file
    res = vn_close(vn);
  }

  vn->flags &= ~VN_OPEN; // clear open flag
  vn_unlock(vn);
  return res;
}

int vn_f_getpage(file_t *file, off_t off, bool cached, __move page_t **page) {
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
      *page = out; // move the page to caller
      res = 0; // success
    }
  } else {
    // getpage from vnode
    res = vn_getpage(vn, off, cached, page);
  }

  vn_unlock(vn);
  return res;
}

ssize_t vn_f_read(file_t *file, kio_t *kio) {
  ssize_t res;
  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

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

  vn_unlock(vn);
  return res;
}

ssize_t vn_f_write(file_t *file, kio_t *kio) {
  vnode_t *vn = file->data;
  if (V_ISDIR(vn)) {
    return -EISDIR; // file is a directory
  } else if (file->access & O_RDONLY) {
    return -EBADF; // file is not open for writing
  } else if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

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

  vn_unlock(vn);
  return res;
}

int vn_f_ioctl(file_t *file, unsigned long request, void *arg) {
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
  vnode_t *vn = file->data;
  if (!vn_lock(vn)) {
    return -EIO; // vnode is dead
  }

  // get the vnode stat
  vn_stat(vn, statbuf);
  vn_unlock(vn);
  return 0;
}

void vn_f_cleanup(file_t *file) {
  vnode_t *vn = moveptr(file->data);
  vn_release(&vn);
}


struct file_ops vnode_file_ops = {
  .f_close = vn_f_close,
  .f_read = vn_f_read,
  .f_write = vn_f_write,
  .f_ioctl = vn_f_ioctl,
  .f_stat = vn_f_stat,
  .f_cleanup = vn_f_cleanup,
};

//
// Vnode API
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

void vn_cleanup(__move vnode_t **vnref) {
  // called when last reference is released
  vnode_t *vn = vn_moveref(vnref);
  DPRINTF("!!! vnode cleanup !!! {:+vn}\n", vn);
  ASSERT(vn != NULL);
  ASSERT(vn->state == V_DEAD);
  ASSERT(ref_count(&vn->refcount) == 0);

  if (VN_OPS(vn)->v_cleanup)
    VN_OPS(vn)->v_cleanup(vn);

  vfs_release(&vn->vfs);
  kfree(vn);
}

bool vn_isatty(vnode_t *vn) {
  if (!V_ISDEV(vn)) {
    return false; // not a character or block device
  }

  // try sending a TIOCGWINSZ ioctl to the device
  struct winsize ws;
  int res = vn_ioctl(vn, TIOCGWINSZ, &ws);
  if (res < 0) {
    // is not a tty device
    return false;
  }
  // is a tty device
  return true;
}

//

int vn_open(vnode_t *vn, int flags) {
  if (!VN_OPS(vn)->v_open)
    return 0;

  // filesystem open
  int res = VN_OPS(vn)->v_open(vn, flags);
  if (res < 0) {
    return res;
  }
  return 0;
}

int vn_close(vnode_t *vn) {
  if (!VN_OPS(vn)->v_close)
    return 0;

  // filesystem close
  int res = VN_OPS(vn)->v_close(vn);
  if (res < 0) {
    return res;
  }
  return 0;
}

int vn_getpage(vnode_t *vn, off_t off, bool cached, __move page_t **result) {
  if (!VN_OPS(vn)->v_getpage) return -ENOTSUP;
  if (off < 0) return -EINVAL;
  if (off >= vn->size)
    return 0;

  page_t *page;
  if (cached && vn->pgcache && (page = pgcache_lookup(vn->pgcache, off)) != NULL) {
    *result = page;
    return 0;
  }

  // filesystem getpage
  int res = VN_OPS(vn)->v_getpage(vn, off, &page);
  if (res < 0) {
    return res;
  }

  *result = page;
  return 0;
}

ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  if (!VN_OPS(vn)->v_read) return -ENOTSUP;
  if (off < 0) return -EINVAL;
  if (off >= vn->size)
    return 0;

  // filesystem read
  return VN_OPS(vn)->v_read(vn, off, kio);
}

ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  if (VFS_ISRDONLY(vn->vfs)) return -EROFS;
  if (!VN_OPS(vn)->v_write) return -ENOTSUP;
  if (off < 0) return -EINVAL;
  if (off >= vn->size)
    return 0;

  // filesystem write
  return VN_OPS(vn)->v_write(vn, off, kio);
}

int vn_ioctl(vnode_t *vn, unsigned long request, void *arg) {
  DPRINTF("ioctl(%d, %#llx, %p)\n", fd, request, argp);
  DPRINTF("TODO: implement ioct (%s:%d)\n", __FILE__, __LINE__);
  return -EOPNOTSUPP; // not implemented yet
}

int vn_fallocate(vnode_t *vn, off_t length) {
  CHECK_WRITE(vn);
  if (length < 0) return -EINVAL;
  if (!VN_OPS(vn)->v_falloc) return -ENOTSUP;

  // filesystem fallocate
  return VN_OPS(vn)->v_falloc(vn, length - vn->size);
}

void vn_stat(vnode_t *vn, struct stat *statbuf) {
  memset(statbuf, 0, sizeof(struct stat));
  statbuf->st_ino = vn->id;
  statbuf->st_mode = vn_to_mode(vn);
  statbuf->st_size = (off_t) vn->size;
  statbuf->st_blocks = (blkcnt_t) vn->blocks;
  statbuf->st_nlink = vn->nlink;
  statbuf->st_rdev = vn->device ? make_dev(vn->device) : 0;
  if (vn->type == V_BLK || vn->type == V_CHR) {
    statbuf->st_dev = make_dev(vn->v_dev);
  }

  // TODO: rest of the fields
}

//

int vn_load(vnode_t *vn) {
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
  CHECK_DIR(vn);
  CHECK_SUPPORTED(vn, v_readdir);
  if (off < 0) return -EINVAL;
  return VN_OPS(vn)->v_readdir(vn, off, dirbuf);
}

//

int vn_lookup(ventry_t *dve, vnode_t *dvn, cstr_t name, __move ventry_t **result) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_create(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_mknod(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, dev_t dev, __move ventry_t **result) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_symlink(ventry_t *dve, vnode_t *dvn, cstr_t name, cstr_t target, __move ventry_t **result) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_hardlink(ventry_t *dve, vnode_t *dvn, cstr_t name, vnode_t *target, __move ventry_t **result) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_unlink(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
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
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_rmdir(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
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
