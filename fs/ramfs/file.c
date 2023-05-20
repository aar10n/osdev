//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include <ramfs/ramfs.h>

int ramfs_f_open(file_t *file) {
  return 0;
}

int ramfs_f_close(file_t *file) {
  return 0;
}

int ramfs_f_sync(file_t *file) {
  return 0;
}

int ramfs_f_truncate(file_t *file, size_t len) {
  ramfs_file_t *ramfs_file = file->inode->data;
  return ramfs_truncate_file(ramfs_file, len);
}

ssize_t ramfs_f_read(file_t *file, off_t off, kio_t *kio) {
  ramfs_file_t *ramfs_file = file->inode->data;
  return ramfs_read_file(ramfs_file, off, kio);
}

ssize_t ramfs_f_write(file_t *file, off_t off, kio_t *kio) {
  ramfs_file_t *ramfs_file = file->inode->data;
  return ramfs_write_file(ramfs_file, off, kio);
}

int ramfs_f_mmap(file_t *file, off_t off, vm_mapping_t *vm) {
  if (off != 0) {
    return -EINVAL;
  }

  ramfs_file_t *ramfs_file = file->inode->data;
  return ramfs_map_file(ramfs_file, vm);
}
