//
// Created by Aaron Gill-Braun on 2023-05-29.
//

#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <bitmap.h>
#include <rb_tree.h>

typedef struct ftable {
  rb_tree_t *tree;
  bitmap_t *bitmap;
  size_t count;
  mtx_t lock;
} ftable_t;

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("file: %s: " fmt, __func__, ##__VA_ARGS__)

#define FTABLE_MAX_FILES 1024

#define FTABLE_LOCK(ftable) mtx_spin_lock(&(ftable)->lock)
#define FTABLE_UNLOCK(ftable) mtx_spin_unlock(&(ftable)->lock)

//

__ref file_t *f_alloc(int fd, int flags, vnode_t *vnode, cstr_t real_path) {
  file_t *file = kmallocz(sizeof(file_t));
  file->fd = fd;
  file->flags = flags;
  file->type = vnode->type;
  file->vnode = vn_getref(vnode);
  file->real_path = str_from_cstr(real_path);
  mtx_init(&file->lock, 0, "file_struct_lock");
  ref_init(&file->refcount);
  return file;
}

__ref file_t *f_dup(file_t *f) {
  file_t *dup = kmallocz(sizeof(file_t));
  dup->fd = f->fd;
  dup->flags = f->flags;
  dup->type = f->type;
  dup->vnode = vn_getref(f->vnode);
  mtx_init(&dup->lock, 0, "file_struct_lock");
  ref_init(&dup->refcount);
  return dup;
}

void f_cleanup(__move file_t **fref) {
  file_t *file = *fref;
  ASSERT(file != NULL);
  ASSERT(file->closed);
  ASSERT(ref_count(&file->refcount) == 0);
  DPRINTF("!!! file cleanup {:file} !!!\n", file);

  vn_release(&file->vnode);
  str_free(&file->real_path);
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

static bool ftable_clone_rb_tree_pred(rb_tree_t *tree, rb_node_t *node, void *data) {
  ftable_t *ftable = data;
  file_t *file = node->data;
  if (!f_lock(file)) {
    return false; // closed
  }

  file_t *dup = f_dup(file);
  rb_tree_insert(ftable->tree, file->fd, dup);
  bitmap_set(ftable->bitmap, (index_t) file->fd);
  ftable->count++;

  f_unlock(file);
  return true;
}

ftable_t *ftable_clone(ftable_t *ftable) {
  ftable_t *clone = kmallocz(sizeof(ftable_t));
  ftable->tree = create_rb_tree();
  ftable->bitmap = create_bitmap(FTABLE_MAX_FILES);
  mtx_init(&ftable->lock, MTX_SPIN, "ftable_lock");

  FTABLE_LOCK(ftable);
  ftable->bitmap = clone_bitmap(ftable->bitmap);

  // typedef bool (*rb_pred_t)(struct rb_tree *, struct rb_node *, void *);

  rb_node_t *node = ftable->tree->min;
  while (node != NULL) {
    file_t *file = node->data;
    if (!f_lock(file)) {
      continue; // closed
    }

    file_t *dup = f_dup(file);
    rb_tree_insert(clone->tree, file->fd, dup);
    bitmap_set(clone->bitmap, (index_t) file->fd);
    clone->count++;

    f_unlock(file);
  }

  FTABLE_UNLOCK(ftable);
  return clone;
}

void ftable_free(ftable_t *ftable) {
  ASSERT(ftable->count == 0);
  rb_tree_free(ftable->tree);
  bitmap_free(ftable->bitmap);
  kfree(ftable);
}

bool ftable_empty(ftable_t *ftable) {
  return ftable->count == 0;
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

__ref file_t *ftable_get_file(ftable_t *ftable, int fd) {
  FTABLE_LOCK(ftable);
  file_t *file = rb_tree_find(ftable->tree, fd);
  file = f_getref(file);
  FTABLE_UNLOCK(ftable);
  return file;
}

__ref file_t *ftable_get_remove_file(ftable_t *ftable, int fd) {
  FTABLE_LOCK(ftable);
  rb_node_t *node = rb_tree_find_node(ftable->tree, fd);
  if (rb_node_is_nil(node)) {
    FTABLE_UNLOCK(ftable);
    return NULL;
  }
  file_t *file = node->data;
  rb_tree_delete_node(ftable->tree, node);
  FTABLE_UNLOCK(ftable);
  return f_moveref(&file);
}

void ftable_add_file(ftable_t *ftable, __move file_t *file) {
  ASSERT(file->fd >= 0 && file->fd < FTABLE_MAX_FILES);
  FTABLE_LOCK(ftable);
  if (rb_tree_find(ftable->tree, file->fd) != NULL) {
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
