//
// Created by Aaron Gill-Braun on 2021-07-27.
//

#include <framebuf.h>
#include <device.h>
#include <mm.h>
#include <panic.h>
#include <thread.h>
#include <string.h>

off_t framebuf_lseek(file_t *file, off_t offset, int origin) {
  ERRNO = ENOTSUP;
  return -1;
}

int framebuf_mmap(file_t *file, uintptr_t vaddr, size_t len, uint16_t flags) {
  device_t *dev = locate_device(file->dentry->inode->dev);
  if (dev == NULL) {
    ERRNO = ENODEV;
    return -1;
  }

  framebuf_t *fb = dev->device;
  fb->vaddr = _vmap_phys_addr(vaddr, fb->paddr, len, flags);
  return 0;
}

file_ops_t framebuf_file_ops = {
  .lseek = framebuf_lseek,
  .mmap = framebuf_mmap,
};

//

void framebuf_fill_inode(device_t *device, inode_t *inode) {
  framebuf_t *fb = device->device;
  inode->size = fb->size;
}

device_ops_t framebuf_device_ops = {
  .fill_inode = framebuf_fill_inode,
};

//

void framebuf_init() {
  // framebuffer
  pixel_format_t fb_format;
  if (boot_info_v2->fb_pixel_format == PIXEL_RGB) {
    fb_format = FB_FORMAT_RGB;
  } else if (boot_info_v2->fb_pixel_format == PIXEL_BGR) {
    fb_format = FB_FORMAT_BGR;
  } else {
    panic("unsupported format");
  }

  framebuf_t *fb = kmalloc(sizeof(framebuf_t));
  fb->paddr = boot_info_v2->fb_addr;
  fb->vaddr = NULL;
  fb->width = boot_info_v2->fb_width;
  fb->height = boot_info_v2->fb_height;
  fb->size = fb->width * fb->height;
  fb->format = fb_format;
  fb->ops = &framebuf_file_ops;

  dev_t fb_dev = fs_register_framebuf(0, fb, &framebuf_device_ops);
  kassert(fb_dev != 0);
  if (fs_mknod("/dev/fb0", S_IFFBF, fb_dev) < 0) {
    panic("failed to create /dev/fb0: %s", strerror(ERRNO));
  }
}

MODULE_INIT(framebuf_init);
