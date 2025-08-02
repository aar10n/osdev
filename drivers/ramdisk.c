//
// Created by Aaron Gill-Braun on 2023-06-23.
//

#include <kernel/device.h>
#include <kernel/mm.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <fs/devfs/devfs.h>

#define ASSERT(x) kassert(x)

struct ramdisk {
  uintptr_t base;   // virtual base address
  size_t size;      // size of the memory region
};


// MARK: Device API

int ramdisk_d_open(device_t *dev, int flags) {
  struct ramdisk *rd = dev->data;
  return 0;
}

int ramdisk_d_close(device_t *dev) {
  struct ramdisk *rd = dev->data;
  return 0;
}

ssize_t ramdisk_d_read(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct ramdisk *rd = dev->data;
  if (off >= rd->size) {
    return 0;
  }

  size_t len = kio_remaining(kio);
  if (nmax > 0 && len > nmax)
    len = nmax;
  
  // calculate how much we can actually read
  size_t available = rd->size - off;
  if (len > available)
    len = available;
  
  return (ssize_t) kio_nwrite_in(kio, (void *)(rd->base + off), available, 0, len);
}

ssize_t ramdisk_d_write(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct ramdisk *rd = dev->data;
  if (off >= rd->size) {
    return 0;
  }

  size_t len = max(kio_remaining(kio), rd->size - off);
  return (ssize_t) kio_nread_out((void *)rd->base, len, off, nmax, kio);
}

__ref page_t *ramdisk_d_getpage(device_t *dev, size_t off) {
  struct ramdisk *rd = dev->data;
  if (off >= rd->size) {
    return NULL;
  }
  return vm_getpage(rd->base + off);
}

static struct device_ops ramdisk_ops = {
  .d_open = ramdisk_d_open,
  .d_close = ramdisk_d_close,
  .d_read = ramdisk_d_read,
  .d_write = ramdisk_d_write,
  .d_getpage = ramdisk_d_getpage,
};

// MARK: Device Registration

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

  devfs_register_class(dev_major_by_name("ramdisk"), -1, "rd", DEVFS_NUMBERED);

  kprintf("ramdisk: registering initrd\n");
  device_t *dev = alloc_device(initrd, &ramdisk_ops, NULL);
  if (register_dev("ramdisk", dev) < 0) {
    panic("failed to register initrd");
  }
}
MODULE_INIT(ramdisk_initrd_module_init);
