//
// Created by Aaron Gill-Braun on 2025-08-25.
//

#define PROCFS_INTERNAL
#include "procfs.h"

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/vfs/vnode.h>

#include <fs/ramfs/ramfs.h>

#include "seqfile.h"

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("procfs: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("procfs: %s: " fmt, __func__, ##__VA_ARGS__)

#define PROCFS_OBJECT(vn) ((procfs_object_t *)((ramfs_node_t *)(vn)->data)->data)

int procfs_f_open(file_t *file, int flags) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_open(file, flags);
  }

  void *handle_data = NULL;
  int res = 0;
  if (obj->ops->proc_open) {
    res = obj->ops->proc_open(obj, flags, &handle_data);
    if (res < 0) {
      return res;
    }
  }
  file->udata = handle_data;
  return 0;
}

int procfs_f_close(file_t *file) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_close(file);
  }

  // move file udata out of file before passing to proc_close
  procfs_handle_t *handle = &(procfs_handle_t){obj, moveptr(file->udata)};
  if (obj->ops->proc_close) {
    return obj->ops->proc_close(handle);
  }
  return 0;
}

int procfs_f_getpage(file_t *file, off_t off, __move struct page **page) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_getpage(file, off, page);
  }

  return -ENOTSUP;
}

ssize_t procfs_f_read(file_t *file, kio_t *kio) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_read(file, kio);
  }

  procfs_handle_t *handle = &(procfs_handle_t){obj, file->udata};
  ssize_t res = obj->ops->proc_read(handle, file->offset, kio);

  if (res > 0) {
    file->offset += res;
  }
  return res;
}

ssize_t procfs_f_write(file_t *file, kio_t *kio) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_write(file, kio);
  }

  ssize_t res = -EINVAL;
  if (obj->ops->proc_write) {
    procfs_handle_t *handle = &(procfs_handle_t){obj, file->udata};
    res = obj->ops->proc_write(handle, file->offset, kio);
  }

  if (res > 0) {
    file->offset += res;
  }
  return res;
}

ssize_t procfs_f_readdir(file_t *file, kio_t *kio) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_readdir(file, kio);
  }

  ssize_t res = 0;
  size_t nbytes = 0;
  procfs_handle_t *handle = &(procfs_handle_t){obj, file->udata};
  while (kio_remaining(kio) > 0) {
    struct dirent dirent;
    res = obj->ops->proc_readdir(handle, &file->offset, &dirent);
    if (res <= 0)
      break;

    size_t n = kio_write_dirent(&dirent, kio);
    if (n != (size_t)res) {
      break; // buffer full
    }

    nbytes += n;
  }

  // file offset was updated by proc_readdir
  return (ssize_t) nbytes;
}

off_t procfs_f_lseek(file_t *file, off_t offset, int whence) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_lseek(file, offset, whence);
  }

  off_t res = -EINVAL;
  if (obj->ops->proc_lseek) {
    procfs_handle_t *handle = &(procfs_handle_t){obj, file->udata};
    res = obj->ops->proc_lseek(handle, offset, whence);
  }

  if (res >= 0) {
    file->offset = res;
  }
  return res;
}

int procfs_f_stat(file_t *file, struct stat *statbuf) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);
  if (!obj || obj->is_static) {
    return vn_f_stat(file, statbuf);
  }

  todo();
}

void procfs_f_cleanup(file_t *file) {
  ASSERT(F_ISVNODE(file));
  vnode_t *vn = file->data;
  procfs_object_t *obj = PROCFS_OBJECT(vn);

  // f_close should have cleaned up udata
  ASSERT(file->udata == NULL);
  vn_f_cleanup(file);
}

