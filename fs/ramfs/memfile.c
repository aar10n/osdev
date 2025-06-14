//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#include "memfile.h"

#include <kernel/mm.h>
#include <kernel/panic.h>
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
  memf->base = vaddr;
  return memf;
}

void memfile_free(memfile_t *memf) {
  if (vmap_free(memf->base, memf->size) < 0) {
    DPRINTF("failed to free vm mapping\n");
  }
  kfree(memf);
}

__ref page_t *memfile_getpage(memfile_t *memf, off_t off) {
  if (off >= memf->size) {
    return NULL;
  }
  return vm_getpage(memf->base + off);
}

int memfile_falloc(memfile_t *memf, size_t newsize) {
  int res;
  if ((res = vmap_resize(memf->base, memf->size, newsize, /*allow_move=*/false, &memf->base)) < 0) {
    DPRINTF("failed to resize vm mapping\n");
    return res;
  }
  memf->size = newsize;
  return 0;
}

ssize_t memfile_read(memfile_t *memf, size_t off, kio_t *kio) {
  return (ssize_t) kio_write_in(kio, (void *)memf->base, memf->size, off);
}

ssize_t memfile_write(memfile_t *memf, size_t off, kio_t *kio) {
  return (ssize_t) kio_read_out((void *)memf->base, memf->size, off, kio);
}
