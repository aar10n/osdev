//
// Created by Aaron Gill-Braun on 2021-04-25.
//

#include <blkdev.h>
#include <mm.h>

#include <string.h>
#include <printf.h>

// TODO: rewrite this
//   - better api
//   - better caching
//   - handle cache invalidation

page_t *alloc_buffer(uint32_t count) {
  page_t *buffer = valloc_pages(SIZE_TO_PAGES(count * SEC_SIZE), PG_WRITE);
  return buffer;
}

void free_buffer(page_t *buffer) {
  vfree_pages(buffer);
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
  return blkdev_readx(dev, lba, count, 0);
}

void *blkdev_readx(blkdev_t *dev, uint64_t lba, uint32_t count, int flags) {
  if (count == 0) {
    return NULL;
  }

  interval_t ivl = intvl(lba, lba + count);
  intvl_node_t *node = intvl_tree_find(dev->cache, ivl);
  if (node != NULL) {
    if (contains(node->interval, ivl) && !(flags & BLKDEV_NOCACHE)) {
      // kprintf("[blkdev] using cache\n");
      // requested data has already been fully read in
      uint64_t offset = ivl.start - node->interval.start;
      page_t *buffer = node->data;
      uintptr_t addr = PAGE_VIRT_ADDR(buffer) + (SEC_SIZE * offset);
      return (void *) addr;
    } else {
      free_buffer(node->data);
      intvl_tree_delete(dev->cache, node->interval);
      return blkdev_readx(dev, lba, count, flags);
    }
  }

  // kprintf("[blkdev] reading from disk\n");
  page_t *buffer = alloc_buffer(count);
  ssize_t result = dev->read(dev->self, lba, count, (void *) PAGE_VIRT_ADDR(buffer));
  if (result < 0) {
    free_buffer(buffer);
    return NULL;
  }

  if (!(flags & BLKDEV_NOCACHE)) {
    intvl_tree_insert(dev->cache, ivl, buffer);
  }
  return (void *) PAGE_VIRT_ADDR(buffer);
}


int blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
  if (count == 0 || buf == NULL) {
    return 0;
  }

  page_t *buffer = _vm_virt_to_page((uintptr_t) buf);
  if (buffer == NULL) {
    return -EINVAL;
  }

  ssize_t result = dev->write(dev->self, lba, count, buf);
  if (result < 0) {
    return result;
  }
  return result;
}

//

int blkdev_readbuf(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
  if (count == 0 || buf == NULL) {
    return 0;
  }

  interval_t ivl = intvl(lba, lba + count);
  intvl_node_t *node = intvl_tree_find(dev->cache, ivl);
  if (node != NULL) {
    if (contains(node->interval, ivl)) {
      // kprintf("[blkdev] using cache\n");
      // requested data has already been fully read in
      uint64_t offset = ivl.start - node->interval.start;
      page_t *buffer = node->data;
      uintptr_t addr = PAGE_VIRT_ADDR(buffer) + (SEC_SIZE * offset);
      memcpy(buf, (void *) addr, count * SEC_SIZE);
      return 0;
    } else {
      free_buffer(node->data);
      intvl_tree_delete(dev->cache, node->interval);
      return blkdev_readbuf(dev, lba, count, buf);
    }
  }

  // kprintf("[blkdev] reading from disk\n");
  ssize_t result = dev->read(dev->self, lba, count, buf);
  if (result < 0) {
    return -EFAILED;
  }
  return result;
}

void blkdev_freebuf(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  vm_mapping_t *mapping = _vmap_get_mapping((uintptr_t) ptr);
  if (mapping == NULL || mapping->type != VM_TYPE_PAGE) {
    return;
  }
  free_buffer(mapping->data.page);
}
