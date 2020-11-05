//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_DEVICE_H
#define FS_DEVICE_H

#include <base.h>

#define FS_MAX_DEVICES 64

typedef struct fs_device fs_device_t;

/* Storage device operations */
typedef struct fs_device_impl {
  ssize_t (*read)(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf);
  ssize_t (*write)(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf);
  int (*release)(fs_device_t *device, void *buf);
} fs_device_impl_t;

/* A storage device */
typedef struct fs_device {
  dev_t id;                    // device id
  void *data;                  // device specific data
  fs_device_impl_t *impl;      // device operations
} fs_device_t;

extern fs_device_impl_t pseudo_impl;

void fs_device_init();
dev_t fs_register_device(void *data, fs_device_impl_t *impl);
fs_device_t *fs_get_device(dev_t id);

#endif
