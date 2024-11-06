//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#ifndef KERNEL_VFS_FILE_H
#define KERNEL_VFS_FILE_H

#include <kernel/vfs_types.h>

typedef struct ftable ftable_t;

/**
 * file is a file descriptor.
 */
typedef struct file {
  int fd;               // file descriptor
  int flags;            // open flags
  enum vtype type;      // vnode type
  vnode_t *vnode;       // vnode reference
  str_t real_path;      // full path to file

  mtx_t lock;           // file lock
  refcount_t refcount;  // reference count
  off_t offset;         // current file offset
  bool closed;          // file closed
} file_t;

__move file_t *f_alloc(int fd, int flags, vnode_t *vnode, cstr_t real_path);
__move file_t *f_dup(file_t *f);
void f_cleanup(__move file_t **fref);

ftable_t *ftable_alloc();
ftable_t *ftable_clone(ftable_t *ftable);
void ftable_free(ftable_t *ftable);
bool ftable_empty(ftable_t *ftable);
int ftable_alloc_fd(ftable_t *ftable);
int ftable_claim_fd(ftable_t *ftable, int fd);
void ftable_free_fd(ftable_t *ftable, int fd);
file_t *ftable_get_file(ftable_t *ftable, int fd) __move;
file_t *ftable_get_remove_file(ftable_t *ftable, int fd) __move;
void ftable_add_file(ftable_t *ftable, __move file_t *file);
void ftable_remove_file(ftable_t *ftable, int fd);

//
//

// #define F_DPRINTF(fmt, ...) kprintf("file: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define F_DPRINTF(fmt, ...)

/// Returns a new reference to the file file.
#define f_getref(f) ({ F_DPRINTF("f_getref {:file} refcount=%d", f, (f) ? ref_count(&(f)->refcount)+1 : 0); _f_getref(f); })
/// Moves the ref out of fref and returns it.
#define f_moveref(fref) _f_moveref(fref) // ({ F_DPRINTF("f_moveref {:ref} refcount=%d", *(fref), (*(fref)) ? ref_count(&(*(fref))->refcount)+1 : 0); _f_moveref(fref); })
/// Moves the ref out of fref and releases it.
#define f_release(fref) ({ F_DPRINTF("f_release {:file} refcount=%d", *(fref), (*(fref)) ? ref_count(&(*(fref))->refcount)-1 : 0); _f_release(fref); })
/// Locks the file.
#define f_lock(f) _f_lock(f, __FILE__, __LINE__)
/// Unlocks the file.
#define f_unlock(f) mtx_unlock(&(f)->lock)


static inline __ref file_t *_f_getref(file_t *f) {
  if (f) ref_get(&f->refcount);
  return f;
}

static inline __ref file_t *_f_moveref(__move file_t **ref) {
  file_t *file = *ref;
  *ref = NULL;
  return file;
}

static inline void _f_release(__move file_t **fref) {
  if (*fref && ref_put(&(*fref)->refcount)) {
    f_cleanup(fref);
  }
}

static inline bool _f_lock(file_t *f, const char *file, int line) {
  if (f->closed) return false;

  _mtx_wait_lock(&f->lock, file, line);
  if (f->closed) {
    _mtx_wait_unlock(&f->lock, file, line);
    return false;
  }
  return true;
}

#endif
