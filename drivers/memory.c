//
// Created by Aaron Gill-Braun on 2023-11-26.
//

#include <kernel/device.h>
#include <kernel/fs.h>
#include <kernel/mm.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("debug: %s: " fmt, __func__, ##__VA_ARGS__)

static int default_d_open(device_t *dev, int flags) { return 0; }
static int default_d_close(device_t *dev) { return 0; }


// Null Device

static ssize_t null_d_read(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  if (off != 0) {
    return -EINVAL;
  }
  return (ssize_t) kio_fill(kio, 0, nmax);
}

static ssize_t null_d_write(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  if (off != 0) {
    return -EINVAL;
  }
  return (ssize_t) kio_drain(kio, nmax);
}

static struct device_ops null_ops = {
  .d_open = default_d_open,
  .d_close = default_d_close,
  .d_read = null_d_read,
  .d_write = null_d_write,
};

// Debug Device

static ssize_t debug_d_read(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  return -EACCES;
}

static ssize_t debug_d_write(device_t *device, size_t off, size_t nmax, kio_t *kio) {
  if (off != 0) {
    return -EINVAL;
  }

  size_t n = 0;
  char ch;
  size_t res;
  while (n < nmax && (res = kio_read_ch(&ch, kio)) > 0) {
    kprintf("%c", ch);
    n++;
  }
  return (ssize_t) kio_transfered(kio);
}

static struct device_ops debug_ops = {
  .d_open = default_d_open,
  .d_close = default_d_close,
  .d_read = debug_d_read,
  .d_write = debug_d_write,
};

//

static void memory_module_init() {
  device_t *devs[] = {
    alloc_device(NULL, &null_ops),
    alloc_device(NULL, &debug_ops),
  };
  size_t num_devs = ARRAY_SIZE(devs);
  for (size_t i = 0; i < num_devs; i++) {
    if (register_dev("memory", devs[i]) < 0) {
      DPRINTF("failed to register device");
      break;
    }
  }
}
MODULE_INIT(memory_module_init);
