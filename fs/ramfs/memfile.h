//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#ifndef FS_RAMFS_MEMFILE_H
#define FS_RAMFS_MEMFILE_H

#include <kernel/mm_types.h>
#include <kernel/kio.h>
#include <kernel/mm/vmalloc.h>

/**
 * A memfile is a memory object with a file-like interface.
 */
typedef struct memfile {
  size_t size;
  vm_mapping_t *vm;
} memfile_t;

// memfile api
memfile_t *memfile_alloc(size_t size);
memfile_t *memfile_alloc_pages(size_t size, page_t *pages);
memfile_t *memfile_alloc_custom(size_t size, vm_getpage_t getpage);
void memfile_free(memfile_t *memf);
int memfile_fallocate(memfile_t *memf, size_t newsize);
ssize_t memfile_read(memfile_t *memf, size_t off, kio_t *kio);
ssize_t memfile_write(memfile_t *memf, size_t off, kio_t *kio);
int memfile_map(memfile_t *memf, size_t off, vm_mapping_t *vm);

#endif
