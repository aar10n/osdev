//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <file.h>
#include <rb_tree.h>
#include <mm/heap.h>
#include <string.h>
#include <process.h>

rb_tree_events_t *file_tree_events = NULL;


void duplicate_file(rb_tree_t *tree, rb_tree_t *new_tree, rb_node_t *node, rb_node_t *new_node) {
  file_t *file = node->data;
  if (file == NULL) {
    return;
  }

  file_t *copy = kmalloc(sizeof(file_t));
  memcpy(copy, file, sizeof(file_t));
  new_node->data = copy;
}

//

file_table_t *create_file_table() {
  file_table_t *table = kmalloc(sizeof(file_table_t));
  table->fds = create_bitmap(MAX_PROC_FILES);
  table->files = create_rb_tree();

  if (file_tree_events == NULL) {
    rb_tree_events_t *events = kmalloc(sizeof(rb_tree_events_t));
    memset(events, 0, sizeof(rb_tree_events_t));
    events->duplicate_node = duplicate_file;
    file_tree_events = events;
  }

  table->files->events = file_tree_events;
  spin_init(&table->lock);
  return table;
}

file_table_t *copy_file_table(file_table_t *table) {
  file_table_t *new_table = kmalloc(sizeof(file_table_t));

  // copy the bitmap
  bitmap_t *bmp = create_bitmap(MAX_PROC_FILES);
  memcpy(bmp->map, table->fds->map, bmp->size);
  bmp->free = table->fds->free;
  bmp->used = table->fds->used;

  // copy the reb-black tree
  rb_tree_t *tree = copy_rb_tree(table->files);

  new_table->fds = bmp;
  new_table->files = tree;
  spin_init(&new_table->lock);
  return new_table;
}

file_t *file_get(int fd) {
  file_table_t *table = FILES;
  lock(table->lock);
  rb_node_t *rb_node = rb_tree_find(table->files, fd);
  unlock(table->lock);

  if (rb_node) {
    return rb_node->data;
  }

  errno = EBADF;
  return NULL;
}

file_t *file_create(fs_node_t *node, int flags) {
  file_table_t *table = FILES;
  file_t *file = kmalloc(sizeof(file_t));

  lock(table->lock);
  index_t index = bitmap_get_set_free(table->fds);
  if (index < 0) {
    unlock(table->lock);

    kfree(file);
    errno = ENFILE;
    return NULL;
  }

  file->fd = (int) index;
  file->flags = flags;
  file->offset = 0;
  file->node = node;
  spinrw_init(&file->lock);

  rb_tree_insert(table->files, file->fd, file);
  unlock(table->lock);

  return file;
}

void file_delete(file_t *file) {
  file_table_t *table = FILES;

  lock(table->lock);
  bitmap_clear(table->fds, file->fd);
  rb_tree_delete(table->files, file->fd);
  unlock(table->lock);
  kfree(file);
}

int file_exists(file_t *file) {
  file_table_t *table = FILES;
  lock(table->lock);
  index_t index = bitmap_get(table->fds, file->fd);
  rb_node_t *rb_node = rb_tree_find(table->files, file->fd);
  unlock(table->lock);

  if (index >= 0 && rb_node != NULL) {
    return 0;
  }

  errno = EBADF;
  return -1;
}

