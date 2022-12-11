//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <fs.h>

static inline dev_t makedev(uint8_t maj, uint8_t min, uint8_t unit) {
  return maj | (min << 8) | (unit << 16);
}
static inline uint8_t major(dev_t dev) {
  return dev & 0xFF;
}
static inline uint16_t minor(dev_t dev) {
  return (dev >> 8) & 0xFF;
}
static inline uint16_t unit(dev_t dev) {
  return (dev >> 16) & 0xFF;
}

dev_t register_blkdev(uint8_t minor, blkdev_t *blkdev, device_ops_t *ops);
dev_t register_chrdev(uint8_t minor, chrdev_t *chrdev, device_ops_t *ops);
dev_t register_framebuf(uint8_t minor, framebuf_t *frambuf, device_ops_t *ops);
device_t *locate_device(dev_t dev);

#endif
