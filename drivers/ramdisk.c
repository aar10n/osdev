//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#include <kernel/device.h>
#include <kernel/fs.h>
#include <kernel/mm.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

struct ramdisk {
  size_t size;      // size of the memory region
  void *base;       // virtual base address
  vm_mapping_t *vm; // virtual mapping
};


// Device API

int ramdisk_d_open(device_t *device, int flags) {
  struct ramdisk *rd = device->data;
  ASSERT(rd->vm != NULL);
  return 0;
}

int ramdisk_d_close(device_t *device) {
  return 0;
}

ssize_t ramdisk_d_read(device_t *device, size_t off, kio_t *kio) {
  struct ramdisk *rd = device->data;
  if (off > rd->size) {
    return -ERANGE;
  }
  return (ssize_t) kio_write(kio, rd->base, rd->size, off);
}

ssize_t ramdisk_d_write(device_t *device, size_t off, kio_t *kio) {
  struct ramdisk *rd = device->data;
  if (off > rd->size) {
    return -ERANGE;
  }
  return (ssize_t) kio_read(kio, rd->base, rd->size, off);
}

page_t *ramdisk_d_getpage(device_t *device, size_t off) {
  struct ramdisk *rd = device->data;
  if (off > rd->size) {
    return NULL;
  }
  return vm_getpage(rd->vm, off, true/* cow */);
}

int ramdisk_d_putpage(device_t *device, size_t off, page_t *page) {
  // TODO: implement
  unimplemented("ramdisk_d_putpage");
}

static struct device_ops ramdisk_ops = {
  .d_open = ramdisk_d_open,
  .d_close = ramdisk_d_close,
  .d_read = ramdisk_d_read,
  .d_write = ramdisk_d_write,
  .d_getpage = ramdisk_d_getpage,
  .d_putpage = ramdisk_d_putpage,
};

static void ramdisk_initrd_module_init() {
  if (boot_info_v2->initrd_addr == 0) {
    panic("initrd not found");
  }

  vm_mapping_t *vm = vm_alloc_phys(boot_info_v2->initrd_addr, 0, boot_info_v2->initrd_size, 0, "initrd");
  if (vm == NULL) {
    panic("failed to map initrd");
  }

  struct ramdisk *initrd = kmallocz(sizeof(struct ramdisk));
  initrd->size = vm->size;
  initrd->base = vm_map(vm, PG_WRITE);
  initrd->vm = vm;

  kprintf("ramdisk: registering initrd\n");
  device_t *dev = alloc_device(initrd, &ramdisk_ops);
  if (register_dev("ramdisk", dev) < 0) {
    panic("failed to register initrd");
  }
}
MODULE_INIT(ramdisk_initrd_module_init);
