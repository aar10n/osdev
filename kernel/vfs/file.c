//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/pipe.h>

#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <bitmap.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
 #define DPRINTF(fmt, ...) kprintf("file: " fmt, ##__VA_ARGS__)
//#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("file: %s: " fmt, __func__, ##__VA_ARGS__)

#define FTABLE_LOCK(ftable) mtx_spin_lock(&(ftable)->lock)
#define FTABLE_UNLOCK(ftable) mtx_spin_unlock(&(ftable)->lock)

extern struct file_ops dev_file_ops;
extern struct file_ops vnode_file_ops;

typedef struct ftable {
  rb_tree_t *tree;
  bitmap_t *bitmap;
  size_t count;
  mtx_t lock;
} ftable_t;

//

__ref fd_entry_t *fd_entry_alloc(int fd, int flags, cstr_t real_path, __ref file_t *file) {
  fd_entry_t *fde = kmallocz(sizeof(fd_entry_t));
  fde->fd = fd;
  fde->flags = flags;
  fde->real_path = str_from_cstr(real_path);
  fde->file = moveref(file);
  ref_init(&fde->refcount);
  mtx_init(&fde->lock, 0, "fd_entry_lock");
  return fde;
}

__ref fd_entry_t *fde_dup(fd_entry_t *fde, int new_fd) {
  file_t *file = fde->file;
  fd_entry_t *dup = kmallocz(sizeof(fd_entry_t));
  dup->fd = (new_fd < 0) ? fde->fd : new_fd;
  
  // copy flags under lock
  mtx_lock(&fde->lock);
  dup->flags = fde->flags;
  mtx_unlock(&fde->lock);
  
  dup->real_path = str_dup(fde->real_path);
  dup->file = f_getref(file); // duplicate the file reference
  ref_init(&dup->refcount);
  mtx_init(&dup->lock, 0, "fd_entry_lock");

  // since the duplicated file is open we bump the file open count
  f_lock(file);
  file->nopen++;
  f_unlock(file);
  return dup;
}

void _fde_cleanup(__move fd_entry_t **fde_ref) {
  fd_entry_t *fde = moveref(*fde_ref);
  ASSERT(fde != NULL);
  ASSERT(ref_count(&fde->refcount) == 0);

  DPRINTF("!!! fd_entry cleanup <{:d}:{:str}> !!!\n", fde->fd, &fde->real_path);
  str_free(&fde->real_path);
  f_putref(&fde->file);
  mtx_destroy(&fde->lock);
  kfree(fde);
}

//

__ref file_t *f_alloc(enum ftype type, int flags, void *data, struct file_ops *ops) {
  file_t *file = kmallocz(sizeof(file_t));
  file->flags = flags & ~O_CLOEXEC;
  file->type = type;
  file->data = data;
  file->ops = ops;
  mtx_init(&file->lock, 0, "file_lock");
  ref_init(&file->refcount);
  return file;
}

__ref file_t *f_alloc_vn(int flags, vnode_t *vn) {
  struct file_ops *ops;
  if (V_ISDEV(vn)) {
    ops = device_get_file_ops(vn->v_dev);
  } else {
    ops = &vnode_file_ops;
  }

  file_t *file = f_alloc(FT_VNODE, flags, vn_getref(vn), ops);
  if (VN_OPS(vn)->v_alloc_file) {
    VN_OPS(vn)->v_alloc_file(vn, file);
  }
  return file;
}

int f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);
  if (file->nopen > 1) {
    // just increment the open count
    DPRINTF("f_close: incrementing count for file %p [nopen %d]\n", file, file->nopen+1);
    file->nopen++;
    return 0;
  }

  int res;
  if ((res = F_OPS(file)->f_open(file, flags)) < 0) {
    EPRINTF("failed to open file {:err}\n", res);
    file->closed = true;
    return res;
  }

  file->nopen++;
  ASSERT(file->nopen == 1);
  return res;
}

int f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  ASSERT(file->nopen > 0);
  if (file->nopen > 1) {
    // just decrement the open count
    DPRINTF("f_close: decrementing count for file %p [nopen %d]\n", file, file->nopen-1);
    file->nopen--;
    return 0; // success
  }

  // close the file
  int res = F_OPS(file)->f_close(file);
  if (res < 0) {
    EPRINTF("failed to close file {:err}\n", res);
    return res;
  }

  file->nopen--;
  file->closed = true;
  return 0;
}

int f_allocate(file_t *file, off_t length) {
  f_lock_assert(file, LA_OWNED);
  int flags = file->flags & O_ACCMODE;
  if (!((flags == O_WRONLY || flags == O_RDWR))) {
    EPRINTF("f_allocate: file %p not opened for writing (flags=0x%x)\n", file, flags);
    return -EBADF; // file must be opened for writing
  } else if (F_OPS(file)->f_allocate == NULL) {
    return -ENOTSUP; // allocate not implemented
  }

  return F_OPS(file)->f_allocate(file, length);
}

ssize_t f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  if (file->flags & O_DIRECTORY) {
    return -EISDIR; // file is a directory
  } else if (file->flags & O_WRONLY) {
    return -EBADF; // file is not open for reading
  } else if (F_OPS(file)->f_read == NULL) {
    return -ENOTSUP; // read not implemented
  }

  return F_OPS(file)->f_read(file, kio);
}

ssize_t f_write(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  if (file->flags & O_DIRECTORY) {
    return -EISDIR; // file is a directory
  } else if (file->flags & O_RDONLY) {
    return -EBADF; // file is not open for writing
  } else if (F_OPS(file)->f_write == NULL) {
    return -ENOTSUP; // write not implemented
  }

  return F_OPS(file)->f_write(file, kio);
}

ssize_t f_readdir(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  if (!(file->flags & O_DIRECTORY)) {
    return -ENOTDIR; // file is not a directory
  } else if (F_OPS(file)->f_readdir == NULL) {
    return -ENOTSUP; // readdir not implemented
  }

  return F_OPS(file)->f_readdir(file, kio);
}

off_t f_lseek(file_t *file, off_t offset, int whence) {
  f_lock_assert(file, LA_OWNED);
  if (F_OPS(file)->f_lseek == NULL) {
    return -ESPIPE; // lseek not supported
  }
  return F_OPS(file)->f_lseek(file, offset, whence);
}

int f_stat(file_t *file, struct stat *statbuf) {
  f_lock_assert(file, LA_OWNED);
  if (F_OPS(file)->f_stat == NULL) {
    return -ENOTSUP; // stat not implemented
  }

  return F_OPS(file)->f_stat(file, statbuf);
}

int f_ioctl(file_t *file, unsigned int request, void *arg) {
  f_lock_assert(file, LA_OWNED);
  if (F_OPS(file)->f_ioctl == NULL) {
    return -ENOTTY; // ioctl not supported
  }
  return F_OPS(file)->f_ioctl(file, request, arg);
}

bool f_isatty(file_t *file) {
  if (!F_ISVNODE(file)) {
    return false;
  }

  vnode_t *vn = file->data;
  if (V_ISDEV(vn)) {
    vn_lock(vn);
    bool res = vn_isatty(vn);
    vn_unlock(vn);
    return res;
  }
  return false;
}

void _f_cleanup(__move file_t **fref) {
  file_t *file = moveref(*fref);
  f_lock_assert(file, LA_NOTOWNED);
  ASSERT(file->closed);
  ASSERT(file->nopen == 0);
  ASSERT(ref_count(&file->refcount) == 0);
  DPRINTF("!!! file cleanup %p !!!\n", file);

  F_OPS(file)->f_cleanup(file);
  ASSERT(file->data == NULL);
  ASSERT(file->udata == NULL);
  mtx_destroy(&file->lock);
  kfree(file);
}

//

ftable_t *ftable_alloc() {
  ftable_t *ftable = kmallocz(sizeof(ftable_t));
  ftable->tree = create_rb_tree();
  ftable->bitmap = create_bitmap(FTABLE_MAX_FILES);
  mtx_init(&ftable->lock, MTX_SPIN, "ftable_lock");
  return ftable;
}

ftable_t *ftable_clone(ftable_t *ftable) {
  ftable_t *clone = kmallocz(sizeof(ftable_t));
  clone->tree = create_rb_tree();
  mtx_init(&clone->lock, MTX_SPIN, "ftable_lock");

  FTABLE_LOCK(ftable);
  clone->bitmap = clone_bitmap(ftable->bitmap);

  rb_node_t *node = ftable->tree->min;
  while (node != ftable->tree->nil) {
    fd_entry_t *fde = node->data;
    fd_entry_t *dup = fde_dup(fde, -1);
    rb_tree_insert(clone->tree, fde->fd, dup);
    clone->count++;

    node = node->next;
  }

  FTABLE_UNLOCK(ftable);
  return clone;
}

void ftable_free(ftable_t **ftablep) {
  ftable_t *ftable = moveref(*ftablep);
  ASSERT(ftable->count == 0);
  rb_tree_free(ftable->tree);
  bitmap_free(ftable->bitmap);
  kfree(ftable);
}


int ftable_alloc_fd(ftable_t *ftable, int at_fd) {
  FTABLE_LOCK(ftable);
  index_t fd_index;
  if (at_fd >= 0) {
    fd_index = bitmap_get_set_free_at(ftable->bitmap, (index_t)at_fd);
  } else {
    fd_index = bitmap_get_set_free(ftable->bitmap);
  }
  FTABLE_UNLOCK(ftable);
  if (fd_index < 0) {
    return -1;
  }
  return (int) fd_index;
}

int ftable_claim_fd(ftable_t *ftable, int fd) {
  if (fd < 0) {
    return -1;
  }
  ASSERT(fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  if (bitmap_get(ftable->bitmap, (index_t) fd)) {
    FTABLE_UNLOCK(ftable);
    return -1;
  }
  bitmap_set(ftable->bitmap, (index_t) fd);
  FTABLE_UNLOCK(ftable);
  return fd;
}

void ftable_free_fd(ftable_t *ftable, int fd) {
  if (fd < 0) {
    return;
  }
  ASSERT(fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  bitmap_clear(ftable->bitmap, (index_t) fd);
  FTABLE_UNLOCK(ftable);
}

__ref fd_entry_t *ftable_get_entry(ftable_t *ftable, int fd) {
  FTABLE_LOCK(ftable);
  fd_entry_t *fde = rb_tree_find(ftable->tree, fd);
  fde = fde_getref(fde);
  FTABLE_UNLOCK(ftable);
  return fde;
}

__ref fd_entry_t *ftable_get_remove_entry(ftable_t *ftable, int fd) {
  FTABLE_LOCK(ftable);
  rb_node_t *node = rb_tree_find_node(ftable->tree, fd);
  if (node == NULL) {
    FTABLE_UNLOCK(ftable);
    return NULL; // entry does not exist
  }

  fd_entry_t *fde = node->data;
  fde = moveref(fde); // move the reference to the caller
  rb_tree_delete_node(ftable->tree, node);
  ftable->count--;
  FTABLE_UNLOCK(ftable);
  return fde;
}

void ftable_add_entry(ftable_t *ftable, __ref fd_entry_t *fde) {
  ASSERT(fde->fd >= 0 && fde->fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  if (rb_tree_find(ftable->tree, fde->fd) != NULL) {
    panic("entry already exists");
  }
  rb_tree_insert(ftable->tree, fde->fd, fde);
  bitmap_set(ftable->bitmap, (index_t) fde->fd);
  ftable->count++;
  FTABLE_UNLOCK(ftable);
}

void ftable_close_exec(ftable_t *ftable) {
  // close directory streams and files opened with the O_CLOEXEC flag
  FTABLE_LOCK(ftable);

  int res;
  rb_node_t *node = ftable->tree->min;
  while (node != ftable->tree->nil) {
    rb_node_t *next = node->next;
    fd_entry_t *fde = node->data;
    
    // check flags under lock
    mtx_lock(&fde->lock);
    bool should_close = fde->flags & (O_DIRECTORY | O_CLOEXEC);
    mtx_unlock(&fde->lock);

    if (!should_close) {
      goto next_entry;
    }

    DPRINTF("close_exec: closing file descriptor {:d} <{:str}>\n", fde->fd, &fde->real_path);

    // close the file
    file_t *file = fde->file;
    if (f_lock(file)) {
      f_close(file);
      f_unlock(file);
    }

    rb_tree_delete_node(ftable->tree, node);
    bitmap_clear(ftable->bitmap, (index_t) fde->fd);
    ftable->count--;
    fde->fd = -1;
    fde_putref(&fde);

  LABEL(next_entry);
    node = next;
  }

  FTABLE_UNLOCK(ftable);
}

void ftable_close_all(ftable_t *ftable) {
  // close all files in the file table
  FTABLE_LOCK(ftable);

  rb_node_t *node = ftable->tree->min;
  while (node != ftable->tree->nil) {
    rb_node_t *next = node->next;
    fd_entry_t *fde = node->data;

    DPRINTF("close_all: closing file descriptor {:d} <{:str}>\n", fde->fd, &fde->real_path);

    // close the file
    file_t *file = fde->file;
    if (f_lock(file)) {
      f_close(file);
      f_unlock(file);
    }

    rb_tree_delete_node(ftable->tree, node);
    bitmap_clear(ftable->bitmap, (index_t) fde->fd);
    ftable->count--;
    fde_putref(&fde);

    node = next;
  }

  FTABLE_UNLOCK(ftable);
}

//
// MARK: File Filter Ops
//

int file_kqfilt_attach(knote_t *kn) {
  int res = 0;
  int fd = (int)kn->event.ident;
  int16_t filter = kn->event.filter;

  fd_entry_t *fde = ftable_get_entry(curproc->files, fd);
  if (fde == NULL)
    return -EBADF;

  fde_lock(fde);
  file_t *file = fde->file;

  if (F_ISVNODE(file)) {
    if (filter == EVFILT_READ) {
      // check if file is open for reading
      if (!F_O_READABLE(fde->flags)) {
        EPRINTF("file_kqfilt_attach: file %d is not open for reading\n", fde->fd);
        goto_res(ret, -EINVAL);
      }
    } else if (filter == EVFILT_WRITE) {
      // EVFILT_WRITE is not supported for vnode files
      EPRINTF("EVFILT_WRITE not supported for vnode files\n");
      goto_res(ret, -EINVAL);
    } else {
      panic("file_kqfilt_attach: unexpected filter %s", evfilt_to_string(filter));
    }

    vnode_t *vn = fde->file->data;
    if (!V_ISREG(vn) && !V_ISDEV(vn)) {
      EPRINTF("file_kqfilt_attach: vnode is not a regular file or device: {:+vn}\n", vn);
      goto_res(ret, -EINVAL);
    }

    if (!vn_lock(vn)) {
      EPRINTF("vnode is dead\n");
      goto_res(ret, -EIO); // vnode is dead
    }

    if (V_ISDEV(vn) && D_OPS((device_t *)vn->v_dev)->d_kqattach) {
      // allow devices to override the kqattach behavior
      device_t *device = vn->v_dev;
      res = D_OPS(device)->d_kqattach(device, kn);
      ASSERT(kn->filt_ops_data != NULL);
    } else {
      kn->filt_ops_data = vn_getref(vn);
      knlist_add(&vn->knlist, kn);
      res = 0; // success
    }

    vn_unlock(vn);
    if (res < 0) {
      EPRINTF("failed to attach knote to vnode {:err}\n", res);
      goto_res(ret, res);
    }
  } else if (F_ISPIPE(file)) {
    // for pipe files, use the pipe_t structure
    pipe_t *pipe = (pipe_t *)file->data;
    kn->filt_ops_data = pipe_getref(pipe);
    kn->knlist = &pipe->knlist;
    knlist_add(&pipe->knlist, kn);
    res = 0;
  } else {
    // handle other file types (pipe, pts, etc.)
    todo("file_kqfilt_attach: implement for file type: %d", file->type);
  }

  kn->fde = fde_getref(fde);

LABEL(ret);
  fde_unlock(fde);
  fde_putref(&fde);
  return res;
}

void file_kqfilt_detach(knote_t *kn) {
  int res = 0;
  fd_entry_t *fde = moveref(kn->fde);
  ASSERT(fde != NULL);
  ASSERT(fde->file != NULL);

  file_t *file = fde->file;
  if (F_ISVNODE(file)) {
    vnode_t *vn = fde->file->data;
    if (V_ISDEV(vn) && D_OPS((device_t *)vn->v_dev)->d_kqdetach) {
      // device is responsible for detaching the knote
      device_t *device = vn->v_dev;
      D_OPS(device)->d_kqdetach(device, kn);
    } else {
      knote_remove_list(kn);
      vnode_t *vnref = moveref(kn->filt_ops_data);
      vn_putref(&vnref);
    }
  } else if (F_ISPIPE(file)) {
    // for pipe files, first remove from whichever list it's on
    knote_remove_list(kn);

    // then release the pipe reference
    pipe_t *pipe_ref = moveref(kn->filt_ops_data);
    if (pipe_ref) {
      pipe_putref(&pipe_ref);
    }
  } else {
    // handle other file types (pts, etc.)
    todo("file_kqfilt_detach: implement for file type: %d", file->type);
  }

  fde_putref(&fde);
}

int file_kqfilt_event(knote_t *kn, long hint) {
  fd_entry_t *fde = kn->fde;
  ASSERT(fde != NULL);
  ASSERT(fde->file != NULL);

  file_t *file = fde->file;
  if (!f_lock(file)) {
    EPRINTF("file is already closed\n");
    kn->event.flags |= EV_EOF; // file is closed, report EOF
    return 1;
  } else if (F_OPS(file)->f_kqevent == NULL) {
    EPRINTF("no kqevent handler for file type %d\n", file->type);
    f_unlock(file);
    return -ENOSYS; // no kqevent handler available
  }

  // defer to the file's kqevent handler
  int res = F_OPS(file)->f_kqevent(file, kn);

  int report = 0;
  if (res < 0) {
    EPRINTF("kqevent handler failed with error {:err}\n", res);
    kn->event.flags |= EV_ERROR; // mark as error
    kn->event.data = (intptr_t)res; // store the error code
    report = 1;
  } else if (res > 0) {
    report = 1;
  }

  f_unlock(file);
  return report;
}


struct filter_ops file_filter_ops = {
  .f_attach = file_kqfilt_attach,
  .f_detach = file_kqfilt_detach,
  .f_event = file_kqfilt_event,
};

void vnode_static_init() {
  register_filter_ops(EVFILT_READ, &file_filter_ops);
  register_filter_ops(EVFILT_WRITE, &file_filter_ops);
}
STATIC_INIT(vnode_static_init);
