//
// Created by Aaron Gill-Braun on 2023-06-03.
//

#ifndef FS_RAMFS_RAMFS_FILE_H
#define FS_RAMFS_RAMFS_FILE_H

#include <mm_types.h>
#include <kio.h>

typedef struct ramfs_file {
  size_t size;
  size_t capacity;
  vm_mapping_t *vm;
} ramfs_file_t;

// ramfs file api
ramfs_file_t *ramfs_file_alloc(size_t initial_size);
void ramfs_file_free(ramfs_file_t *file);
int ramfs_file_truncate(ramfs_file_t *file, size_t newsize);
ssize_t ramfs_file_read(ramfs_file_t *file, size_t off, kio_t *kio);
ssize_t ramfs_file_write(ramfs_file_t *file, size_t off, kio_t *kio);
int ramfs_file_map(ramfs_file_t *file, vm_mapping_t *vm);

#endif
