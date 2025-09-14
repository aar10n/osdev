//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#ifndef FS_RAMFS_MEMFILE_H
#define FS_RAMFS_MEMFILE_H

#include <kernel/base.h>
#include <kernel/kio.h>
#include <kernel/mm_types.h>

/**
 * A memfile is a memory object with a file-like interface.
 */
typedef struct memfile {
  uintptr_t base;
  size_t size;        // current file size
  size_t mapped_size; // current VM mapping size
} memfile_t;

// memfile api
memfile_t *memfile_alloc(size_t size);
void memfile_free(memfile_t *memf);
__ref page_t *memfile_getpage(memfile_t *memf, off_t off);
int memfile_falloc(memfile_t *memf, size_t newsize);
ssize_t memfile_read(memfile_t *memf, size_t off, kio_t *kio);
ssize_t memfile_write(memfile_t *memf, size_t off, kio_t *kio);

#endif
