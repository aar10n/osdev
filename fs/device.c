//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <device.h>
#include <atomic.h>
#include <stdio.h>
#include <vfs.h>

static dev_t __device_id = 0;
fs_device_t fs_devices[FS_MAX_DEVICES] = {};

static inline dev_t alloc_device_id() {
  return atomic_fetch_add(&__device_id, 1);
}

// pseudo device

ssize_t pseudo_read(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf) {
  return 0;
}
ssize_t pseudo_write(fs_device_t *device, uint64_t lba, uint32_t seccount, void **buf) {
  return 0;
}
int pseudo_release(fs_device_t *device, void *buf) {
  return 0;
}

fs_device_impl_t pseudo_impl = {
  pseudo_read, pseudo_write, pseudo_release
};

//

dev_t fs_register_device(void *data, fs_device_impl_t *impl) {
  if (__device_id >= FS_MAX_DEVICES) {
    errno = EOVERFLOW;
    return -1;
  }

  dev_t id = alloc_device_id();
  kprintf("[fs] registering device (id %d)\n", id);
  fs_devices[id].id = id;
  fs_devices[id].data = data;
  fs_devices[id].impl = impl;

  vfs_add_device(&fs_devices[id]);
  return id;
}

fs_device_t *fs_get_device(dev_t id) {
  if (id >= FS_MAX_DEVICES) {
    errno = EINVAL;
    return NULL;
  }

  if (fs_devices[id].id != id) {
    errno = ENODEV;
    return NULL;
  }
  return &fs_devices[id];
}
