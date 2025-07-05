//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#ifndef KERNEL_VFS_FILE_H
#define KERNEL_VFS_FILE_H

#include <kernel/vfs_types.h>

#define FTABLE_MAX_FILES 1024

#define F_OPS(f) __type_checked(struct file *, f, (f)->ops)

typedef struct ftable ftable_t;

__ref fd_entry_t *fd_entry_alloc(int fd, int flags, cstr_t real_path, __ref file_t *file);
__ref fd_entry_t *fde_dup(fd_entry_t *fde, int new_fd);
void _fde_cleanup(__move fd_entry_t **fde_ref);

__ref file_t *f_alloc(enum ftype type, int access, void *data, struct file_ops *ops);
__ref file_t *f_alloc_vn(int access, vnode_t *vnode);
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
void ftable_exec_close(ftable_t *ftable);

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

#define f_lock_assert(f, what) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  mtx_assert(&(f)->lock, what); \
})
#define f_lock(f) ({ \
  ASSERT_IS_TYPE(file_t *, f); \
  mtx_lock(&(f)->lock); \
  bool _locked = true; \
  if ((f)->closed) { \
    mtx_unlock(&(f)->lock);  \
    _locked = false; \
  } \
  _locked;\
})
#define f_unlock(f) mtx_unlock(&(f)->lock)

#endif
