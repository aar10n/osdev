//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#ifndef KERNEL_VFS_FILE_H
#define KERNEL_VFS_FILE_H

#include <kernel/vfs_types.h>
#include <kernel/mutex.h>

typedef struct ftable ftable_t;

/**
 * file is a file descriptor.
 */
typedef struct file {
  int fd;               // file descriptor
  int flags;            // open flags
  enum vtype type;      // vnode type
  vnode_t *vnode;       // vnode reference
  ventry_t *ventry;     //

  mutex_t lock;         // file lock
  refcount_t refcount;  // reference count
  off_t offset;         // current file offset
  bool closed;          // file closed
} file_t;

static inline file_t *f_getref(file_t *file) __move {
  if (file) ref_get(&file->refcount);
  return file;
}

static inline file_t *f_moveref(__move file_t **ref) __move {
  file_t *file = *ref;
  *ref = NULL;
  return file;
}

file_t *f_alloc(int fd, int flags, vnode_t *vnode) __move;
void f_release(__move file_t **ref);

ftable_t *ftable_alloc();
void ftable_free(ftable_t *ftable);
int ftable_alloc_fd(ftable_t *ftable);
void ftable_free_fd(ftable_t *ftable, int fd);
file_t *ftable_get_file(ftable_t *ftable, int fd) __move;
file_t *ftable_get_remove_file(ftable_t *ftable, int fd) __move;
void ftable_add_file(ftable_t *ftable, __move file_t *file);
void ftable_remove_file(ftable_t *ftable, int fd);

//
//

static bool f_lock(file_t *file) {
  if (file->closed) return false;
  mutex_lock(&file->lock);
  if (file->closed) {
    mutex_unlock(&file->lock); return false;
  }
  return true;
}

static void f_unlock(file_t *file) {
  mutex_unlock(&file->lock);
}

#endif
