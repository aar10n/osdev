//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <file.h>
#include <rb_tree.h>
#include <mm/heap.h>

file_t *__create_file() {
  file_t *file = kmalloc(sizeof(file_t));
  file->fd = -1;
  file->flags = -1;
  file->offset = 0;
  spinrw_init(&file->lock);
  return file;
}

file_table_t *__create_file_table() {
  file_table_t *table = kmalloc(sizeof(file_table_t));
  table->next_fd = 0;
  table->files = create_rb_tree();
  spin_init(&table->lock);
  return table;
}
