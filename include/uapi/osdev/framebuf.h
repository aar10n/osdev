//
// Created by Aaron Gill-Braun on 2025-07-28.
//

#ifndef INCLUDE_UAPI_FRAMEBUF_H
#define INCLUDE_UAPI_FRAMEBUF_H

#include <bits/ioctl.h>
#include <stdint.h>

struct fb_info {
  uint64_t size; // size of the framebuffer in bytes
  uint32_t xres; // horizontal resolution in pixels
  uint32_t yres; // vertical resolution in pixels
  uint32_t bits_per_pixel;  // bits per pixel
};

#define FBIOGETINFO _IOR('F', 0x01, struct fb_info) // get framebuffer info

#endif
