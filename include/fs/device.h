//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <fs.h>

dev_t register_blkdev(uint8_t minor, blkdev_t *blkdev);
dev_t register_chrdev(uint8_t minor, chrdev_t *chrdev);
device_t *locate_device(dev_t dev);

#endif
