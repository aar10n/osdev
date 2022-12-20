//
// Created by Aaron Gill-Braun on 2022-12-15.
//

#include <initrd/initrd.h>
#include <thread.h>
#include <string.h>


int initrd_open(file_t *file, dentry_t *dentry) {
  return 0;
}

int initrd_flush(file_t *file) {
  return 0;
}

ssize_t initrd_read(file_t *file, char *buf, size_t count, off_t *offset) {
  inode_t *inode = file->dentry->inode;
  if (inode->size == 0 || file->pos >= inode->size) {
    return 0;
  }

  ssize_t len = min(*offset + count, inode->size - *offset);
  void *data = inode->data;
  memcpy(buf, data, len);
  *offset += len;
  return len;
}

ssize_t initrd_write(file_t *file, const char *buf, size_t count, off_t *offset) {
  ERRNO = EROFS;
  return -1;
}
