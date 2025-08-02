//
// Created by Aaron Gill-Braun on 2025-07-27.
//

#include <kernel/device.h>
#include <kernel/mm.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <fs/devfs/devfs.h>
#include <uapi/osdev/framebuf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("framebuf: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("framebuf: %s: " fmt, __func__, ##__VA_ARGS__)

struct framebuf {
  uintptr_t base;   // virtual base address
  size_t size;      // size of the memory region
  uint32_t width;   // width of the framebuffer
  uint32_t height;  // height of the framebuffer
  uint32_t bits_per_pixel; // bits per pixel (bpp)
};

// MARK: Device API

int framebuf_d_open(device_t *dev, int flags) {
  struct framebuf *fb = dev->data;
  return 0;
}

int framebuf_d_close(device_t *dev) {
  struct framebuf *fb = dev->data;
  return 0;
}

ssize_t framebuf_d_read(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct framebuf *fb = dev->data;
  if (off >= fb->size) {
    return 0;
  }

  size_t len = kio_remaining(kio);
  if (nmax > 0 && len > nmax)
    len = nmax;
  return (ssize_t) kio_nwrite_in(kio, (void *)fb->base, fb->size, off, nmax);
}

ssize_t framebuf_d_write(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct framebuf *fb = dev->data;
  if (off >= fb->size) {
    return 0;
  }

  size_t len = max(kio_remaining(kio), fb->size - off);
  return (ssize_t) kio_nread_out((void *)fb->base, len, off, nmax, kio);
}

void framebuf_d_stat(device_t *dev, struct stat *st) {
  struct framebuf *fb = dev->data;
  st->st_size = (off_t) fb->size;
  st->st_blksize = PAGE_SIZE; // typical block size
  st->st_blocks = (off_t) SIZE_TO_PAGES(fb->size);
}

int framebuf_d_ioctl(device_t *dev, unsigned int request, void *arg) {
  struct framebuf *fb = dev->data;
  DPRINTF("framebuf_d_ioctl: request %llu, arg %p\n", request, arg);
  if (request == FBIOGETINFO) {
    if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
      EPRINTF("FBIOGETINFO ioctl requires a valid argument\n");
      return -EINVAL; // invalid argument
    }

    DPRINTF("FBIOGETINFO ioctl\n");
    struct fb_info *fb_info = arg;
    fb_info->size = fb->size;
    fb_info->xres = fb->width;
    fb_info->yres = fb->height;
    fb_info->bits_per_pixel = fb->bits_per_pixel;
    return 0;
  } else {
    EPRINTF("framebuf_d_ioctl: unsupported request %llu\n", request);
    return -ENOTTY; // not a tty device or not supported
  }
}

__ref page_t *framebuf_d_getpage(device_t *dev, size_t off) {
  struct framebuf *fb = dev->data;
  if (off >= fb->size) {
    return NULL;
  }
  return alloc_nonowned_pages_at(boot_info_v2->fb_addr + off, 1, PAGE_SIZE);
}

static struct device_ops framebuf_ops = {
  .d_open = framebuf_d_open,
  .d_close = framebuf_d_close,
  .d_read = framebuf_d_read,
  .d_write = framebuf_d_write,
  .d_stat = framebuf_d_stat,
  .d_ioctl = framebuf_d_ioctl,
  .d_getpage = framebuf_d_getpage,
};

// MARK: Device Registration

static void framebuf_module_init() {
  if (boot_info_v2->fb_addr == 0) {
    panic("framebuffer not found");
  }

  struct framebuf *fb = kmallocz(sizeof(struct framebuf));
  fb->base = FRAMEBUFFER_VA;
  fb->size = boot_info_v2->fb_size;
  fb->width = boot_info_v2->fb_width;
  fb->height = boot_info_v2->fb_height;
  fb->bits_per_pixel = 32;

  devfs_register_class(dev_major_by_name("framebuf"), -1, "fb", DEVFS_NUMBERED);

  kprintf("framebuf: registering framebuffer\n");
  device_t *dev = alloc_device(fb, &framebuf_ops, NULL);
  if (register_dev("framebuf", dev) < 0) {
    panic("failed to register framebuf");
  }
}
MODULE_INIT(framebuf_module_init);
