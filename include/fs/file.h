//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_FILE_H
#define FS_FILE_H

#include <fs.h>
#include <bitmap.h>
#include <rb_tree.h>
#include <spinlock.h>


typedef struct file_table {
  bitmap_t *fds;
  rb_tree_t *files;
  spinlock_t lock;
} file_table_t;

file_table_t *create_file_table();
file_table_t *copy_file_table(file_table_t *table);

//

file_t *f_alloc(dentry_t *dentry, int flags);
file_t *f_dup(file_t *file, int fd);
void f_release(file_t *file);
file_t *f_locate(int fd);

int f_open(file_t *file, dentry_t *dentry);
int f_flush(file_t *file);

ssize_t f_read(file_t *file, char *buf, size_t count);
ssize_t f_write(file_t *file, const char *buf, size_t count);
off_t f_lseek(file_t *file, off_t offset, int whence);
dentry_t *f_readdir(file_t *file);
int f_mmap(file_t *file, uintptr_t vaddr, size_t len, uint16_t flags);

#endif
