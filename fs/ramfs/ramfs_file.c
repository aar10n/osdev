//
// Created by Aaron Gill-Braun on 2023-06-03.
//

#include "ramfs_file.h"

#include <mm.h>
#include <kio.h>
#include <panic.h>
#include <printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs_file: %s: " fmt, __func__, ##__VA_ARGS__)

#define RAMFS_PG_FLAGS (PG_WRITE | PG_USER | PG_WRITETHRU)


static inline void *get_backing_mem(ramfs_file_t *file) {
  return PAGE_VIRT_ADDRP(file->pages);
}

static inline void alloc_backing_mem(ramfs_file_t *file, size_t size) {
  if (size == 0) {
    return;
  }

  file->pages = valloc_pages(SIZE_TO_PAGES(size), RAMFS_PG_FLAGS);
  file->size = size;
  file->capacity = PAGES_TO_SIZE(SIZE_TO_PAGES(size));
}

static inline void free_backing_mem(ramfs_file_t *file) {
  vfree_pages(file->pages);
  file->pages = NULL;
}

// resizes and or allocates backing memory for a file to the given size.
// if the new size is less than the current capacity, the backing memory
// will be resized down if the new size is less than half the capacity.
static inline void resize_file(ramfs_file_t *file, size_t newsize) {
  if (newsize == file->capacity) {
    return;
  }

  if (file->capacity == 0) {
    // first time allocating backing memory
    alloc_backing_mem(file, newsize);
    return;
  }

  if (newsize < file->capacity) {
    if (newsize == 0) {
      // truncate the file
      free_backing_mem(file);
      file->size = 0;
      file->capacity = 0;
      return;
    }

    // if the new size is less than half the capacity, resize it down
    if (newsize < (file->capacity / 2)) {
      size_t npages = SIZE_TO_PAGES(newsize);
      if (PAGES_TO_SIZE(npages) == file->capacity) {
        // dont change backing memory
        file->size = newsize;
        return;
      }

      page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
      memcpy(PAGE_VIRT_ADDRP(newpages), PAGE_VIRT_ADDRP(file->pages), newsize);
      vfree_pages(file->pages);

      file->pages = newpages;
      file->size = newsize;
      file->capacity = PAGES_TO_SIZE(npages);
      return;
    }

    // otherwise dont change backing memory
    file->size = newsize;
    return;
  }

  // if the new size is greater than the capacity, resize it up
  size_t npages = SIZE_TO_PAGES(newsize);
  page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
  memcpy(PAGE_VIRT_ADDRP(newpages), PAGE_VIRT_ADDRP(file->pages), file->size);
  vfree_pages(file->pages);

  file->pages = newpages;
  file->size = newsize;
  file->capacity = PAGES_TO_SIZE(npages);
}

//
// MARK: RamFS File API
//

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
  resize_file(file, newsize);
  return 0;
}

ssize_t ramfs_file_read(ramfs_file_t *file, size_t off, kio_t *kio) {
  return (ssize_t) kio_movein(kio, get_backing_mem(file), file->size, off);
}

ssize_t ramfs_file_write(ramfs_file_t *file, size_t off, kio_t *kio) {
  if (off >= file->size) {
    resize_file(file, off + kio->size);
  }
  return (ssize_t) kio_moveout(kio, get_backing_mem(file), file->size, off);
}

int ramfs_file_map(ramfs_file_t *file, vm_mapping_t *vm) {
  if (vm->type != VM_TYPE_RSVD) {
    return -EINVAL;
  }

  if (vm->size > file->size) {
    resize_file(file, vm->size);
  }

  // map the file into memory
  if (_vmap_reserved_shortlived(vm, file->pages) == NULL) {
    DPRINTF("failed to map file\n");
    return -EFAILED;
  }
  return 0;
}

