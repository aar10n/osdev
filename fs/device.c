//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#include <device.h>
#include <thread.h>
#include <printf.h>
#include <queue.h>
#include <atomic.h>


static uint8_t __minor_ids[3] = { 1, 1, 1 };
LIST_HEAD(device_t) devices[3][32] = {};
spinlock_t lock = {};


dev_t register_device(uint8_t major, uint8_t minor, void *data, device_ops_t *ops) {
  if (minor == 0) {
    minor = atomic_fetch_add(&__minor_ids[major], 1);
  }

  uint8_t u = 0;
  device_t *d;
  LIST_FOREACH(d, &devices[major][minor], devices) {
    u = unit(d->dev);
  }

  device_t *device = kmalloc(sizeof(device_t));
  memset(device, 0, sizeof(device_t));

  u++;
  device->dev = makedev(major, minor, u);
  device->device = data;
  device->ops = ops;

  LIST_ADD(&devices[major][minor], device, devices);
  return makedev(major, minor, u);
}

//

dev_t register_blkdev(uint8_t minor, blkdev_t *blkdev, device_ops_t *ops) {
  return register_device(DEVICE_BLKDEV, minor, blkdev, ops);
}

dev_t register_chrdev(uint8_t minor, chrdev_t *chrdev, device_ops_t *ops) {
  return register_device(DEVICE_CHRDEV, minor, chrdev, ops);
}

dev_t register_framebuf(uint8_t minor, framebuf_t *frambuf, device_ops_t *ops) {
  return register_device(DEVICE_FB, minor, frambuf, ops);
}

device_t *locate_device(dev_t dev) {
  uint8_t maj = major(dev);
  uint8_t min = minor(dev);
  uint8_t uni = unit(dev);

  device_t *d;
  LIST_FOREACH(d, &devices[maj][min], devices) {
    if (unit(d->dev) == uni) {
      return d;
    }
  }

  ERRNO = ENODEV;
  return NULL;
}

