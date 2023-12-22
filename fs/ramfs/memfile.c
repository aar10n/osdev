//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#include "memfile.h"

#include <kernel/mm.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("memfile: %s: " fmt, __func__, ##__VA_ARGS__)

//

memfile_t *memfile_alloc(size_t size) {
  memfile_t *memf = kmallocz(sizeof(memfile_t));
  memf->size = size;

  uintptr_t vaddr = vmap_anon(SIZE_1GB, 0, size, VM_WRITE, "memfile");
  if (vaddr == 0) {
    DPRINTF("failed to allocate vm mapping\n");
    kfree(memf);
    return NULL;
  }
  memf->vm = vm_get_mapping(vaddr);
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
