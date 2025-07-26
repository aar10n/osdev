//
// Created by Aaron Gill-Braun on 2023-11-26.
//

#include <kernel/device.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/tty.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <fs/devfs/devfs.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("memory: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("memory: %s: " fmt, __func__, ##__VA_ARGS__)

static int default_d_open(device_t *dev, int flags) { return 0; }
static int default_d_close(device_t *dev) { return 0; }

//
// MARK: Null Device
//

static ssize_t null_d_read(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  return (ssize_t) kio_fill(kio, 0, nmax);
}

static ssize_t null_d_write(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  return (ssize_t) kio_drain(kio, nmax);
}

static struct device_ops null_ops = {
  .d_open = default_d_open,
  .d_close = default_d_close,
  .d_read = null_d_read,
  .d_write = null_d_write,
};

//
// MARK: Debug Device
//

static ssize_t debug_d_read(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  return -EACCES;
}

static ssize_t debug_d_write(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  size_t n = 0;
  char ch;
  size_t res;
  while (n < nmax && (res = kio_read_ch(&ch, kio)) > 0) {
    kprintf("%c", ch);
    n++;
  }
  return (ssize_t) kio_transfered(kio);
}

static int debug_d_ioctl(device_t *dev, unsigned long request, void *arg) {
  if (request == TIOCGWINSZ) {
    DPRINTF("TIOCGWINSZ ioctl\n");

    // simulate a terminal window size
    if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
      EPRINTF("TIOCGWINSZ ioctl requires a valid argument\n");
      return -EINVAL; // invalid argument
    }

    *((struct winsize *)arg) = (struct winsize) {
      .ws_row = 24, // 24 rows
      .ws_col = 80, // 80 columns
      .ws_xpixel = 0,
      .ws_ypixel = 0,
    };
    return 0; // success
  }
  return -ENOTTY;
}

static struct device_ops debug_ops = {
  .d_open = default_d_open,
  .d_close = default_d_close,
  .d_read = debug_d_read,
  .d_write = debug_d_write,
  .d_ioctl = debug_d_ioctl,
};

//
// MARK: Loopback Device
//

static ssize_t loopback_d_read(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  return (ssize_t) kio_fill(kio, 0, nmax);
}

static ssize_t loopback_d_write(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  return (ssize_t) kio_drain(kio, nmax);
}

static struct device_ops loopback_ops = {
  .d_open = default_d_open,
  .d_close = default_d_close,
  .d_read = loopback_d_read,
  .d_write = loopback_d_write,
};

//
// MARK: Device Registration
//

static void memory_module_init() {
  devfs_register_class(dev_major_by_name("memory"), 0, "null", 0);
  devfs_register_class(dev_major_by_name("memory"), 1, "debug", 0);
  devfs_register_class(dev_major_by_name("loop"), -1, "loop", DEVFS_NUMBERED);

  device_t *mem_devs[] = {
    alloc_device(NULL, &null_ops),
    alloc_device(NULL, &debug_ops),
  };
  for (size_t i = 0; i < ARRAY_SIZE(mem_devs); i++) {
    if (register_dev("memory", mem_devs[i]) < 0) {
      DPRINTF("failed to register device\n");
      free_device(mem_devs[i]);
      continue;
    }
  }

  device_t *loopback_dev = alloc_device(NULL, &loopback_ops);
  if (register_dev("loop", loopback_dev) < 0) {
    DPRINTF("failed to register loopback device\n");
    free_device(loopback_dev);
  } else {
    DPRINTF("loopback device registered successfully\n");
  }
}
MODULE_INIT(memory_module_init);
