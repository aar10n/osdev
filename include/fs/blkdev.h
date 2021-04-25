//
// Created by Aaron Gill-Braun on 2021-04-25.
//

#ifndef FS_BLKDEV_H
#define FS_BLKDEV_H

#include <base.h>

#define SEC_SIZE 512

typedef struct {

} block_cache_t;

// Block Device
typedef struct blkdev {
  uint32_t flags;   // device flags
  void *self;       // device specific pointer

  // device operations
  ssize_t (*read)(void *self, uint64_t lba, uint32_t count, void *buffer);
  ssize_t (*write)(void *self, uint64_t lba, uint32_t count, void *buffer);
} blkdev_t;


ssize_t blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void **buf);
ssize_t blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);

#endif
