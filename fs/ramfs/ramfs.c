//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include <ramfs/ramfs.h>

#include <mm.h>
#include <kio.h>
#include <panic.h>
#include <printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ramfs: %s: " fmt, __func__, ##__VA_ARGS__)

#define RAMFS_PG_FLAGS (PG_WRITE | PG_USER | PG_WRITETHRU)


static inline void *get_backing_mem(ramfs_file_t *file) {
  switch (file->type) {
    case RAMFS_MEM_HEAP:
      return file->heap;
    case RAMFS_MEM_PAGE:
      return PAGE_VIRT_ADDRP(file->page);
    default:
      panic("invalid ramfs file type");
  }
}

static inline void alloc_backing_mem(ramfs_file_t *file, size_t size) {
  if (size == 0) {
    return;
  }

  switch (file->type) {
    case RAMFS_MEM_HEAP:
      file->heap = kmallocz(size);
      file->size = size;
      file->capacity = size;
      break;
    case RAMFS_MEM_PAGE:
      file->page = valloc_pages(SIZE_TO_PAGES(size), RAMFS_PG_FLAGS);
      file->size = size;
      file->capacity = PAGES_TO_SIZE(SIZE_TO_PAGES(size));
      break;
    default:
      panic("invalid ramfs file type");
  }
}

static inline void free_backing_mem(ramfs_file_t *file) {
  switch (file->type) {
    case RAMFS_MEM_HEAP:
      kfree(file->heap);
      file->heap = NULL;
      break;
    case RAMFS_MEM_PAGE:
      vfree_pages(file->page);
      file->page = NULL;
      break;
    default:
      panic("invalid ramfs file type");
  }
}

static inline int convert_mem_type(ramfs_file_t *file, ramfs_mem_t type) {
  // converts the backing memory from one to another if permissible
  // this only fails if called on a RAMFS_MEM_PAGE file and its size
  // is greater than the max heap allocation size. in this case no
  // conversion occurs.
  if (file->type == type) {
    return 0;
  }

  if (file->type == RAMFS_MEM_HEAP) {
    // heap -> page
    ASSERT(type == RAMFS_MEM_PAGE);
    size_t npages = SIZE_TO_PAGES(file->size);
    page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
    memcpy(PAGE_VIRT_ADDRP(newpages), file->heap, file->size);
    kfree(file->heap);

    file->type = RAMFS_MEM_PAGE;
    file->page = newpages;
  } else if (file->type == RAMFS_MEM_PAGE) {
    // page -> heap
    ASSERT(type == RAMFS_MEM_HEAP);
    if (file->size > CHUNK_MAX_SIZE) {
      // cannot convert to heap
      return -1;
    }

    size_t size = PAGES_TO_SIZE(file->capacity);
    void *newheap = kmallocz(size);
    memcpy(newheap, PAGE_VIRT_ADDRP(file->page), size);
    vfree_pages(file->page);

    file->type = RAMFS_MEM_HEAP;
    file->heap = newheap;
  } else {
    panic("invalid ramfs file type");
  }
  return 0;
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
      if (file->type == RAMFS_MEM_HEAP) {
        void *newheap = kmallocz(newsize);
        memcpy(newheap, file->heap, newsize);
        kfree(file->heap);

        file->heap = newheap;
        file->size = newsize;
        file->capacity = newsize;
        return;
      } else if (file->type == RAMFS_MEM_PAGE) {
        size_t npages = SIZE_TO_PAGES(newsize);
        if (PAGES_TO_SIZE(npages) == file->capacity) {
          // dont change backing memory
          file->size = newsize;
          return;
        }

        page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
        memcpy(PAGE_VIRT_ADDRP(newpages), PAGE_VIRT_ADDRP(file->page), newsize);
        vfree_pages(file->page);

        file->page = newpages;
        file->size = newsize;
        file->capacity = PAGES_TO_SIZE(npages);
        return;
      }
      unreachable;
    }

    // otherwise dont change backing memory
    file->size = newsize;
    return;
  }

  // if the new size is greater than the capacity, resize it up
  if (file->type == RAMFS_MEM_HEAP) {
    if (newsize > CHUNK_MAX_SIZE) {
      // promote to page-backed memory
      size_t npages = SIZE_TO_PAGES(newsize);
      page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
      memcpy(PAGE_VIRT_ADDRP(newpages), file->heap, file->size);
      kfree(file->heap);

      file->type = RAMFS_MEM_PAGE;
      file->page = newpages;
      file->size = newsize;
      file->capacity = PAGES_TO_SIZE(npages);
      return;
    }

    void *newheap = kmallocz(newsize);
    memcpy(newheap, file->heap, file->size);
    kfree(file->heap);

    file->heap = newheap;
    file->size = newsize;
    file->capacity = newsize;
    return;
  } else if (file->type == RAMFS_MEM_PAGE) {
    size_t npages = SIZE_TO_PAGES(newsize);
    page_t *newpages = valloc_named_pagesz(npages, RAMFS_PG_FLAGS, "ramfs file");
    memcpy(PAGE_VIRT_ADDRP(newpages), PAGE_VIRT_ADDRP(file->page), file->size);
    vfree_pages(file->page);

    file->page = newpages;
    file->size = newsize;
    file->capacity = PAGES_TO_SIZE(npages);
    return;
  }
  unreachable;
}

//
// MARK: RamFS File API
//

ramfs_file_t *ramfs_alloc_file(size_t size) {
  if (size < PAGE_SIZE) {
    return ramfs_alloc_file_type(RAMFS_MEM_HEAP, size);
  }
  return ramfs_alloc_file_type(RAMFS_MEM_PAGE, size);
}

ramfs_file_t *ramfs_alloc_file_type(ramfs_mem_t type, size_t size) {
  ramfs_file_t *file = kmallocz(sizeof(ramfs_file_t));
  file->type = type;
  alloc_backing_mem(file, size);
  return file;
}

void ramfs_free_file(ramfs_file_t *file) {
  if (file == NULL)
    return;

  free_backing_mem(file);
  kfree(file);
}

int ramfs_truncate_file(ramfs_file_t *file, size_t newsize) {
  resize_file(file, newsize);
  return 0;
}

ssize_t ramfs_read_file(ramfs_file_t *file, size_t off, kio_t *kio) {
  return (ssize_t) kio_movein(kio, get_backing_mem(file), file->size, off);
}

ssize_t ramfs_write_file(ramfs_file_t *file, size_t off, kio_t *kio) {
  if (off >= file->size) {
    resize_file(file, off + kio->size);
  }
  return (ssize_t) kio_moveout(kio, get_backing_mem(file), file->size, off);
}

int ramfs_map_file(ramfs_file_t *file, vm_mapping_t *vm) {
  if (vm->type != VM_TYPE_RSVD) {
    return -EINVAL;
  }

  // ensure the file is page-backed
  convert_mem_type(file, RAMFS_MEM_PAGE);
  if (vm->size > file->size) {
    resize_file(file, vm->size);
  }

  // map the file into memory
  if (_vmap_reserved_shortlived(vm, file->page) == NULL) {
    DPRINTF("failed to map file\n");
    return -EFAILED;
  }
  return 0;
}
