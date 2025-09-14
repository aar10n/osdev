//
// Created by Aaron Gill-Braun on 2023-06-24.
//

#include "memfile.h"

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...)
//#define DPRINTF(fmt, ...) kprintf("memfile: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("memfile: %s: " fmt, __func__, ##__VA_ARGS__)

//

memfile_t *memfile_alloc(size_t size) {
  memfile_t *memf = kmallocz(sizeof(memfile_t));
  memf->size = size;

  // for empty files, allocate a minimum of 1 page to allow for growth
  size_t initial_mapped_size = size > 0 ? page_align(size) : PAGE_SIZE;
  uintptr_t vaddr = vmap_anon(SIZE_1GB, 0, initial_mapped_size, VM_WRITE, "memfile");
  if (vaddr == 0) {
    DPRINTF("failed to allocate vm mapping\n");
    kfree(memf);
    return NULL;
  }
  memf->base = vaddr;
  memf->mapped_size = initial_mapped_size;
  return memf;
}

void memfile_free(memfile_t *memf) {
  if (!memf) {
    return;
  }

  if (vmap_free(memf->base, memf->mapped_size) < 0) {
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
  DPRINTF("falloc: newsize=%zu\n", newsize);
  // check if we need to grow the VM mapping
  size_t new_mapped_size = page_align(newsize);
  if (new_mapped_size > memf->mapped_size) {
    int res;
    if ((res = vmap_resize(memf->base, memf->mapped_size, new_mapped_size, /*allow_move=*/false, &memf->base)) < 0) {
      DPRINTF("failed to resize vm mapping from %zu to %zu\n", memf->mapped_size, new_mapped_size);
      return res;
    }
    memf->mapped_size = new_mapped_size;
  }
  memf->size = newsize;
  return 0;
}

ssize_t memfile_read(memfile_t *memf, size_t off, kio_t *kio) {
  DPRINTF("read: off=%zu, size=%zu, kio_remaining=%zu\n", off, memf->size, kio_remaining(kio));
  if (off >= memf->size) {
    DPRINTF("read: eof (off=%zu >= size=%zu)\n", off, memf->size);
    return 0;
  }

  ssize_t nbytes = (ssize_t) kio_write_in(kio, (void *)memf->base, memf->size, off);
  DPRINTF("read: nbytes=%zd\n", nbytes);
  return nbytes;
}

ssize_t memfile_write(memfile_t *memf, size_t off, kio_t *kio) {
  size_t write_len = kio_remaining(kio);
  size_t required_size = off + write_len;
  DPRINTF("write: off=%zu, write_len=%zu, required_size=%zu, current_size=%zu, kio_remaining=%zu\n",
          off, write_len, required_size, memf->size, kio_remaining(kio));

  // grow the file if necessary
  if (required_size > memf->size) {
    DPRINTF("write: growing file from %zu to %zu bytes\n", memf->size, required_size);
    int res = memfile_falloc(memf, required_size);
    if (res < 0) {
      EPRINTF("failed to grow memfile to %zu bytes: {:err}\n", required_size, res);
      return res;
    }
  }

  // now write the data
  ssize_t nbytes = (ssize_t) kio_read_out((void *)memf->base, memf->mapped_size, off, kio);
  DPRINTF("write: nbytes=%zd\n", nbytes);
  return nbytes;
}
