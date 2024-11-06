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
  uintptr_t base;   // virtual base address
  size_t size;      // size of the memory region
};


// Device API

int ramdisk_d_open(device_t *device, int flags) {
  struct ramdisk *rd = device->data;
  return 0;
}

int ramdisk_d_close(device_t *device) {
  struct ramdisk *rd = device->data;
  return 0;
}

ssize_t ramdisk_d_read(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  struct ramdisk *rd = device->data;
  if (off >= rd->size) {
    return 0;
  }

  size_t len = kio_remaining(kio);
  if (nmax > 0 && len > nmax)
    len = nmax;
  return (ssize_t) kio_nwrite_in(kio, (void *)rd->base, rd->size, off, nmax);
}

ssize_t ramdisk_d_write(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  struct ramdisk *rd = device->data;
  if (off >= rd->size) {
    return 0;
  }

  size_t len = kio_remaining(kio);
  return (ssize_t) kio_nread_out((void *)rd->base, len, off, nmax, kio);
}

__ref page_t *ramdisk_d_getpage(device_t *device, size_t off) {
  struct ramdisk *rd = device->data;
  if (off >= rd->size) {
    return NULL;
  }
  return vm_getpage_cow(rd->base + off);
}

int ramdisk_d_putpage(device_t *device, size_t off, __move page_t *page) {
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

  uintptr_t vaddr = vmap_phys(boot_info_v2->initrd_addr, 0, boot_info_v2->initrd_size, VM_READ, "initrd");
  if (vaddr == 0) {
    panic("failed to map initrd");
  }

  struct ramdisk *initrd = kmallocz(sizeof(struct ramdisk));
  initrd->base = vaddr;
  initrd->size = boot_info_v2->initrd_size;

  kprintf("ramdisk: registering initrd\n");
  device_t *dev = alloc_device(initrd, &ramdisk_ops);
  if (register_dev("ramdisk", dev) < 0) {
    panic("failed to register initrd");
  }
}
MODULE_INIT(ramdisk_initrd_module_init);
