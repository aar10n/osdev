//
// Created by Aaron Gill-Braun on 2021-04-25.
//

#ifndef FS_BLKDEV_H
#define FS_BLKDEV_H

#include <base.h>
#include <interval_tree.h>

#define SEC_SIZE 512
#define SIZE_TO_SECS(size) (align(size, SEC_SIZE) / 512)

// flags
#define BLKDEV_NOCACHE 0x0001


// Block Device
typedef struct blkdev {
  uint32_t flags;       // device flags
  void *self;           // device specific pointer
  intvl_tree_t *cache;  // block cache

  // device operations
  ssize_t (*read)(void *self, uint64_t lba, uint32_t count, void *buffer);
  ssize_t (*write)(void *self, uint64_t lba, uint32_t count, void *buffer);
} blkdev_t;


blkdev_t *blkdev_init(void *self, void *read, void *write);
void *blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count);
void *blkdev_readx(blkdev_t *dev, uint64_t lba, uint32_t count, int flags);
int blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);
int blkdev_readbuf(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);
void blkdev_freebuf(void *ptr);

#endif
