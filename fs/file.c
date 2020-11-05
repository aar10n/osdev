//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <file.h>
#include <rb_tree.h>
#include <mm/heap.h>

// #define FILES (current->files)
#define FILES (PERCPU->files)


file_table_t *create_file_table() {
  file_table_t *table = kmalloc(sizeof(file_table_t));
  table->fds = create_bitmap(MAX_PROC_FILES);
  table->files = create_rb_tree();
  spin_init(&table->lock);
  return table;
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
  percpu_t *pcpu = PERCPU;
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
  rb_tree_delete(table->files, file->fd);
  unlock(table->lock);
  kfree(file);
}

