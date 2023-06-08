//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#include <vfs/file.h>
#include <vfs/vnode.h>

#include <panic.h>
#include <printf.h>
#include <bitmap.h>
#include <rb_tree.h>

typedef struct ftable {
  rb_tree_t *tree;
  bitmap_t *bitmap;
  size_t count;
  spinlock_t lock;
} ftable_t;

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("file: %s: " fmt, __func__, ##__VA_ARGS__)

#define FTABLE_MAX_FILES 1024

#define FTABLE_LOCK(ftable) SPIN_LOCK(&(ftable)->lock)
#define FTABLE_UNLOCK(ftable) SPIN_UNLOCK(&(ftable)->lock)

static void f_cleanup(file_t *file) {
  vn_release(&file->vnode);
  kfree(file);
}

//

file_t *f_alloc(int fd, int flags, vnode_t *vnode) __move {
  file_t *file = kmallocz(sizeof(file_t));
  file->fd = fd;
  file->flags = flags;
  file->type = vnode->type;
  file->vnode = vn_getref(vnode);
  mutex_init(&file->lock, 0);
  ref_init(&file->refcount);
  return file;
}

void f_release(__move file_t **ref) {
  if (*ref == NULL) {
    return;
  }

  file_t *file = f_moveref(ref);
  if (*ref) {
    if (ref_put(&(*ref)->refcount, NULL)) {
      f_cleanup(*ref);
      *ref = NULL;
    }
  }
}

//

ftable_t *ftable_alloc() {
  ftable_t *ftable = kmallocz(sizeof(ftable_t));
  ftable->tree = create_rb_tree();
  ftable->bitmap = create_bitmap(FTABLE_MAX_FILES);
  spin_init(&ftable->lock);
  return ftable;
}

void ftable_free(ftable_t *ftable) {
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

void ftable_free_fd(ftable_t *ftable, int fd) {
  if (fd < 0) {
    return;
  }
  ASSERT(fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  bitmap_clear(ftable->bitmap, (index_t) fd);
  FTABLE_UNLOCK(ftable);
}

file_t *ftable_get_file(ftable_t *ftable, int fd) __move {
  FTABLE_LOCK(ftable);
  file_t *file = rb_tree_get(ftable->tree, fd);
  FTABLE_UNLOCK(ftable);
  return f_getref(file);
}

file_t *ftable_get_remove_file(ftable_t *ftable, int fd) __move {
  FTABLE_LOCK(ftable);
  rb_node_t *node = rb_tree_find(ftable->tree, fd);
  if (node == NULL) {
    FTABLE_UNLOCK(ftable);
    return NULL;
  }
  file_t *file = node->data;
  rb_tree_delete(ftable->tree, fd);
  FTABLE_UNLOCK(ftable);
  return f_moveref(&file);
}

void ftable_add_file(ftable_t *ftable, __move file_t *file) {
  ASSERT(file->fd >= 0 && file->fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  if (rb_tree_get(ftable->tree, file->fd) != NULL) {
    panic("file already exists");
  }
  rb_tree_insert(ftable->tree, file->fd, file);
  ftable->count++;
  FTABLE_UNLOCK(ftable);
}

void ftable_remove_file(ftable_t *ftable, int fd) {
  ASSERT(fd >= 0 && fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  file_t *file = rb_tree_delete(ftable->tree, fd);
  if (file == NULL) {
    panic("file does not exist");
  }
  ftable->count--;
  FTABLE_UNLOCK(ftable);
  f_release(&file);
}
