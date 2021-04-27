//
// Created by Aaron Gill-Braun on 2021-04-25.
//

#include <fs/blkdev.h>
#include <mm.h>
#include <printf.h>

page_t *alloc_buffer(uint32_t count) {
  page_t *buffer = alloc_zero_pages(SIZE_TO_PAGES(count * SEC_SIZE), PE_WRITE);
  return buffer;
}

void free_buffer(page_t *buffer) {
  vm_unmap_page(buffer);
  free_page(buffer);
}

//

blkdev_t *blkdev_init(void *self, void *read, void *write) {
  blkdev_t *blkdev = kmalloc(sizeof(blkdev_t));
  blkdev->flags = 0;
  blkdev->self = self;
  blkdev->read = read;
  blkdev->write = write;
  blkdev->cache = create_intvl_tree();

  return blkdev;
}

void *blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count) {
  if (count == 0) {
    return NULL;
  }

  interval_t ivl = intvl(lba, lba + count);
  intvl_node_t *node = intvl_tree_find(dev->cache, ivl);
  if (node != NULL) {
    if (contains(node->interval, ivl)) {
      kprintf("[blkdev] using cache\n");
      // requested data has already been fully read in
      uint64_t offset = ivl.start - node->interval.start;
      page_t *buffer = node->data;
      uintptr_t addr = buffer->addr + (SEC_SIZE * offset);
      return (void *) addr;
    } else {
      free_buffer(node->data);
      intvl_tree_delete(dev->cache, node->interval);
      return blkdev_read(dev, lba, count);
    }
  }

  kprintf("[blkdev] reading from disk\n");
  page_t *buffer = alloc_buffer(count);
  ssize_t result = dev->read(dev->self, lba, count, (void *) buffer->addr);
  if (result < 0) {
    free_buffer(buffer);
    return NULL;
  }

  intvl_tree_insert(dev->cache, ivl, buffer);
  return (void *) buffer->addr;
}


int blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
  if (count == 0 || buf == NULL) {
    return 0;
  }

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
