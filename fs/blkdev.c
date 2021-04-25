//
// Created by Aaron Gill-Braun on 2021-04-25.
//

#include <fs/blkdev.h>
#include <mm.h>

ssize_t blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void **buf) {
  if (count == 0 || buf == NULL) {
    return 0;
  }

  size_t size = count * SEC_SIZE;
  page_t *buffer = alloc_zero_pages(SIZE_TO_PAGES(size), PE_WRITE);
  ssize_t result = dev->read(dev->self, lba, count, (void *) buffer->addr);
  if (result < 0) {
    vm_unmap_page(buffer);
    free_page(buffer);
    *buf = NULL;
    return result;
  }

  *buf = (void *) buffer->addr;
  return result;
}

ssize_t blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
  if (count == 0 || buf == NULL) {
    return 0;
  }

  size_t size = count * SEC_SIZE;
  page_t *buffer = vm_get_page((uintptr_t) buf);
  if (buffer == NULL) {
    return -EINVAL;
  }

  ssize_t result = dev->write(dev->self, lba, count, buf);
  if (result < 0) {
    return result;
  }
  return result;
}
