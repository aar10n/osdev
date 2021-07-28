//
// Created by Aaron Gill-Braun on 2021-07-27.
//

#ifndef FS_FRAMEBUF_H
#define FS_FRAMEBUF_H

#include <base.h>

typedef struct file_ops file_ops_t;

typedef enum pixel_format {
  FB_FORMAT_RGB,
  FB_FORMAT_BGR,
} pixel_format_t;

typedef struct framebuf {
  uintptr_t paddr;
  void *vaddr;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  pixel_format_t format;
  file_ops_t *ops;
} framebuf_t;

void framebuf_init();

#endif
