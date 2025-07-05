//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <bitmap.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...)
// #define DPRINTF(fmt, ...) kprintf("file: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("file: %s: " fmt, __func__, ##__VA_ARGS__)

#define FTABLE_LOCK(ftable) mtx_spin_lock(&(ftable)->lock)
#define FTABLE_UNLOCK(ftable) mtx_spin_unlock(&(ftable)->lock)

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
  return fde;
}

__ref fd_entry_t *fde_dup(fd_entry_t *fde, int new_fd) {
  fd_entry_t *dup = kmallocz(sizeof(fd_entry_t));
  dup->fd = (new_fd < 0) ? fde->fd : new_fd;
  dup->flags = fde->flags;
  dup->real_path = str_dup(fde->real_path);
  dup->file = f_getref(fde->file); // duplicate the file reference
  ref_init(&dup->refcount);
  return dup;
}

void _fde_cleanup(__move fd_entry_t **fde_ref) {
  fd_entry_t *fde = moveref(*fde_ref);
  ASSERT(fde != NULL);
  ASSERT(ref_count(&fde->refcount) == 0);
  DPRINTF("!!! fd_entry cleanup <{:d}:{:str}> !!!\n", fde->fd, &fde->real_path);

  str_free(&fde->real_path);
  f_putref(&fde->file);
  kfree(fde);
}

//

__ref file_t *f_alloc(enum ftype type, int access, void *data, struct file_ops *ops) {
  file_t *file = kmallocz(sizeof(file_t));
  file->access = access & O_ACCMODE;
  file->type = type;
  file->data = data;
  file->ops = ops;
  mtx_init(&file->lock, 0, "file_struct_lock");
  ref_init(&file->refcount);
  return file;
}

__ref file_t *f_alloc_vn(int access, vnode_t *vnode) {
  file_t *file = f_alloc(FT_VNODE, access, vn_getref(vnode), &vnode_file_ops);
  return file;
}

void _f_cleanup(__move file_t **fref) {
  file_t *file = moveref(*fref);
  ASSERT(file != NULL);
  ASSERT(ref_count(&file->refcount) == 0);
  DPRINTF("!!! file cleanup {:file} !!!\n", file);

  int res = F_OPS(file)->f_close(file);
  if (res < 0) {
    EPRINTF("failed to close file {:err}\n", res);
  }

  F_OPS(file)->f_cleanup(file);
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


int ftable_alloc_fd(ftable_t *ftable) {
  FTABLE_LOCK(ftable);
  index_t fd = bitmap_get_set_free(ftable->bitmap);
  FTABLE_UNLOCK(ftable);
  if (fd < 0) {
    return -1;
  }
  return (int) fd;
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

void ftable_exec_close(ftable_t *ftable) {
  // close all directory streams and files opened with the O_CLOEXEC flag
  FTABLE_LOCK(ftable);

  rb_node_t *node = ftable->tree->min;
  while (node != ftable->tree->nil) {
    rb_node_t *next = node->next;
    fd_entry_t *fde = node->data;
    if (fde->flags & (O_DIRECTORY | O_CLOEXEC)) {
      // remove the entry
      rb_tree_delete_node(ftable->tree, node);
      bitmap_clear(ftable->bitmap, (index_t) fde->fd);
      ftable->count--;
      fde->fd = -1;
      fde_putref(&fde);
    }

    node = next;
  }

  FTABLE_UNLOCK(ftable);
}
