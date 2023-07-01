//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#include "memfile.h"

#include <kernel/mm.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("memfile: %s: " fmt, __func__, ##__VA_ARGS__)

// called when a fault occurs in an unmapped part of a memfile
static page_t *memfile_get_page(vm_mapping_t *vm, size_t off, uint32_t vm_flags, void *data) {
  memfile_t *memf = data;
  if (off >= memf->size) {
    return NULL;
  }

  // allocate a new page
  page_t *page = alloc_pages_size(1, vm_flags_to_size(vm_flags));
  if (page == NULL) {
    DPRINTF("failed to allocate page\n");
    return NULL;
  }
  return page;
}

//

memfile_t *memfile_alloc(size_t size) {
  memfile_t *memf = kmallocz(sizeof(memfile_t));
  memf->size = size;

  vm_file_t *file = vm_file_alloc(size, memfile_get_page, memf);
  vm_mapping_t *vm = vmap_file(file, 0, size, VM_WRITE, "memfile");
  if (vm == NULL) {
    DPRINTF("failed to allocate vm mapping\n");
    vm_file_free(file);
    kfree(memf);
    return NULL;
  }
  memf->vm = vm;
  return memf;
}

memfile_t *memfile_alloc_pages(size_t size, page_t *pages) {
  memfile_t *memf = memfile_alloc(size);
  if (memf == NULL) {
    return NULL;
  }

  vm_mapping_t *vm = memf->vm;
  if (vm_putpages(vm, pages, 0) < 0) {
    DPRINTF("failed to map pages into memfile\n");
    memfile_free(memf);
    return NULL;
  }
  return memf;
}

memfile_t *memfile_alloc_custom(size_t size, vm_getpage_t getpage) {
  memfile_t *memf = kmallocz(sizeof(memfile_t));
  memf->size = size;

  vm_file_t *file = vm_file_alloc(size, getpage, memf);
  vm_mapping_t *vm = vmap_file(file, 0, size, VM_WRITE, "memfile");
  if (vm == NULL) {
    DPRINTF("failed to allocate vm mapping\n");
    kfree(memf);
    return NULL;
  }
  memf->vm = vm;
  return memf;
}

void memfile_free(memfile_t *memf) {
  vm_mapping_t *vm = memf->vm;
  vmap_free(vm);
  kfree(memf);
}

int memfile_fallocate(memfile_t *memf, size_t newsize) {
  vm_mapping_t *vm = memf->vm;
  if (vm_resize(vm, newsize, true) < 0) {
    DPRINTF("failed to resize vm mapping\n");
    return -1;
  }

  memf->size = newsize;
  return 0;
}

ssize_t memfile_read(memfile_t *memf, size_t off, kio_t *kio) {
  vm_mapping_t *vm = memf->vm;
  return (ssize_t) kio_write_in(kio, (void *) vm->address, memf->size, off);
}

ssize_t memfile_write(memfile_t *memf, size_t off, kio_t *kio) {
  vm_mapping_t *vm = memf->vm;
  return (ssize_t) kio_read_out((void *) vm->address, memf->size, off, kio);
}

int memfile_map(memfile_t *memf, size_t off, vm_mapping_t *vm) {
  unimplemented("memfile_map");
}
