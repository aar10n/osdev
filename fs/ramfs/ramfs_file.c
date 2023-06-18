//
// Created by Aaron Gill-Braun on 2023-06-03.
//

#include "ramfs_file.h"

//

#include <kernel/mm.h>
#include <kernel/kio.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs_file: %s: " fmt, __func__, ##__VA_ARGS__)

#define RAMFS_PG_FLAGS (PG_WRITE | PG_USER | PG_WRITETHRU)


static inline void *get_backing_mem(ramfs_file_t *file) {
  return (void *) file->vm->address;
}

static inline void alloc_backing_mem(ramfs_file_t *file, size_t size) {
  if (size == 0) {
    return;
  }

  // file->vm = vm_alloc_pages();
  file->size = size;
  file->capacity = PAGES_TO_SIZE(SIZE_TO_PAGES(size));
}

static inline void free_backing_mem(ramfs_file_t *file) {
  if (file->vm == NULL) {
    return;
  }
  // vm_free(file->vm);
}

//
// MARK: RamFS File API
//

// TODO: fix this with new vm api

ramfs_file_t *ramfs_file_alloc(size_t size) {
  ramfs_file_t *file = kmallocz(sizeof(ramfs_file_t));
  alloc_backing_mem(file, size);
  return file;
}

void ramfs_file_free(ramfs_file_t *file) {
  if (file == NULL)
    return;

  free_backing_mem(file);
  kfree(file);
}

int ramfs_file_truncate(ramfs_file_t *file, size_t newsize) {
  // resize_file(file, newsize);
  return 0;
}

ssize_t ramfs_file_read(ramfs_file_t *file, size_t off, kio_t *kio) {
  return (ssize_t) kio_write(kio, get_backing_mem(file), file->size, off);
}

ssize_t ramfs_file_write(ramfs_file_t *file, size_t off, kio_t *kio) {
  if (off >= file->size) {
    // resize_file(file, off + kio->size);
  }
  return (ssize_t) kio_read(kio, get_backing_mem(file), file->size, off);
}

int ramfs_file_map(ramfs_file_t *file, vm_mapping_t *vm) {
  if (vm->type != VM_TYPE_RSVD) {
    return -EINVAL;
  }

  if (vm->size > file->size) {
    // resize_file(file, vm->size);
  }

  // // map the file into memory
  // if (_vmap_reserved_shortlived(vm, file->pages) == NULL) {
  //   DPRINTF("failed to map file\n");
  //   return -EFAILED;
  // }
  return 0;
}

