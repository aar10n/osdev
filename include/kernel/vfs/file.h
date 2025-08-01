//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#ifndef KERNEL_VFS_FILE_H
#define KERNEL_VFS_FILE_H

#include <kernel/vfs_types.h>

#define FTABLE_MAX_FILES 1024

#define F_OPS(f) __type_checked(struct file *, f, (f)->ops)

#define F_O_READABLE(flags) ((flags) & (O_RDONLY | O_RDWR))
#define F_O_WRITEABLE(flags) ((flags) & (O_WRONLY | O_RDWR))

typedef struct ftable ftable_t;

__ref fd_entry_t *fd_entry_alloc(int fd, int flags, cstr_t real_path, __ref file_t *file);
__ref fd_entry_t *fde_dup(fd_entry_t *fde, int new_fd);
void _fde_cleanup(__move fd_entry_t **fde_ref);

__ref file_t *f_alloc(enum ftype type, int flags, void *data, struct file_ops *ops);
__ref file_t *f_alloc_vn(int flags, vnode_t *vn);
int f_open(file_t *file, int flags);
int f_close(file_t *file);
ssize_t f_read(file_t *file, kio_t *kio);
ssize_t f_write(file_t *file, kio_t *kio);
bool f_isatty(file_t *file);
void _f_cleanup(__move file_t **fref);

ftable_t *ftable_alloc();
ftable_t *ftable_clone(ftable_t *ftable);
void ftable_free(ftable_t **ftablep);
int ftable_alloc_fd(ftable_t *ftable);
int ftable_claim_fd(ftable_t *ftable, int fd);
void ftable_free_fd(ftable_t *ftable, int fd);
__ref fd_entry_t *ftable_get_entry(ftable_t *ftable, int fd);
__ref fd_entry_t *ftable_get_remove_entry(ftable_t *ftable, int fd);
void ftable_add_entry(ftable_t *ftable, __ref fd_entry_t *fde);
void ftable_close_exec(ftable_t *ftable);
void ftable_close_all(ftable_t *ftable);



// #define F_DPRINTF(fmt, ...) kprintf("file: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__)
#define F_DPRINTF(fmt, ...)

#define fde_getref(fde) ({ \
  ASSERT_IS_TYPE(fd_entry_t *, fde); \
  (fde) ? ref_get(&(fde)->refcount) : NULL; \
  (fde); \
})
#define fde_putref(fderef) ({ \
  ASSERT_IS_TYPE(fd_entry_t **, fderef); \
  fd_entry_t *__fde = moveref(*(fderef)); \
  if (__fde) { \
    if (ref_put(&__fde->refcount)) { \
      _fde_cleanup(&__fde); \
    } \
  } \
})

#define fde_lock(fde) ({ \
  ASSERT_IS_TYPE(fd_entry_t *, fde); \
  mtx_lock(&(fde)->lock); \
})
#define fde_unlock(fde) ({ \
  ASSERT_IS_TYPE(fd_entry_t *, fde); \
  mtx_unlock(&(fde)->lock); \
})

#define f_getref(f) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  (f) ? ref_get(&(f)->refcount) : NULL; \
  (f); \
})
#define f_putref(fref) ({ \
  ASSERT_IS_TYPE(file_t **, fref); \
  file_t *__f = moveref(*(fref)); \
  if (__f) { \
    if (ref_put(&__f->refcount)) { \
      _f_cleanup(&__f); \
    } \
  } \
})
#define f_lock(f) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  mtx_lock(&(f)->lock); \
  bool _locked = true; \
  if ((f)->closed) { \
    mtx_unlock(&(f)->lock);  \
    _locked = false; \
  } else {           \
    F_DPRINTF("f_lock: locking file %p", f); \
  } \
  _locked;\
})
#define f_unlock(f) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  F_DPRINTF("f_unlock: unlocking file %p", f); \
  mtx_unlock(&(f)->lock); \
})
#define f_unlock_putref(f) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  f_unlock(f);                 \
  f_putref(&(f));              \
})

#define f_lock_assert(f, what) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  mtx_assert(&(f)->lock, what); \
})

#endif
