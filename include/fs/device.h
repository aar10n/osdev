//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <fs.h>

dev_t register_blkdev(uint8_t minor, blkdev_t *blkdev, device_ops_t *ops);
dev_t register_chrdev(uint8_t minor, chrdev_t *chrdev, device_ops_t *ops);
dev_t register_framebuf(uint8_t minor, framebuf_t *frambuf, device_ops_t *ops);
device_t *locate_device(dev_t dev);

#endif
