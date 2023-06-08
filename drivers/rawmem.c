//
// Created by Aaron Gill-Braun on 2023-04-28.
//

#include <drivers/rawmem.h>

#include <device.h>
#include <fs.h>
#include <mm.h>

#include <printf.h>
#include <panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("rawmem: %s: " fmt "\n", __func__, ##__VA_ARGS__)

// Device API

struct rawmem_device {
  uintptr_t phys; // physical addr base
  void *base;     // virtual addr base
  size_t size;    // size of the memory region
};


int rawmem_d_open(device_t *device) {
  struct rawmem_device *dev = device->data;
  ASSERT(dev->base != NULL);
  // TODO: map phys to virt of not already and unmap on close
  return -1;
}

int rawmem_d_close(device_t *device) {
  return 0;
}

ssize_t rawmem_d_read(device_t *device, size_t off, kio_t *kio) {
  struct rawmem_device *dev = device->data;
  if (off > dev->size) {
    return -ERANGE;
  }
  return (ssize_t) kio_movein(kio, dev->base, dev->size, off);
}

ssize_t rawmem_d_write(device_t *device, size_t off, kio_t *kio) {
  struct rawmem_device *dev = device->data;
  if (off > dev->size) {
    return -ERANGE;
  }
  return (ssize_t) kio_moveout(kio, dev->base, dev->size, off);
}

static struct device_ops rawmem_ops = {
  .d_open = rawmem_d_open,
  .d_close = rawmem_d_close,
  .d_read = rawmem_d_read,
  .d_write = rawmem_d_write,
};

static void rawmem_initrd_module_init() {
  if (boot_info_v2->initrd_addr == 0) {
    return;
  }

  struct rawmem_device *initrd_dev = kmallocz(sizeof(struct rawmem_device));
  vm_mapping_t *initrd = _vmap_get_mapping(boot_info_v2->initrd_addr);
  ASSERT(initrd != NULL);
  initrd_dev->base = (void *) initrd->address;
  initrd_dev->phys = initrd->data.phys;
  initrd_dev->size = initrd->size;

  kprintf("rawmem: registering initrd device\n");

  device_t *dev = alloc_device(initrd_dev, &rawmem_ops);
  if (register_dev("mem", dev) < 0) {
    DPRINTF("failed to register device");
    free_device(dev);
    kfree(initrd_dev);
  }
}
MODULE_INIT(rawmem_initrd_module_init);
