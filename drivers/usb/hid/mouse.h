//
// Created by Aaron Gill-Braun on 2021-09-11.
//

#ifndef KERNEL_USB_MOUSE_H
#define KERNEL_USB_MOUSE_H

#include <kernel/base.h>

#include "hid.h"

typedef struct {
  uint8_t buttons_offset;
  uint8_t x_offset;
  uint8_t y_offset;

  uint8_t prev_buttons;
} hid_mouse_t;


hid_mouse_t *hid_mouse_init(report_format_t *format);
void hid_mouse_handle_input(hid_device_t *hid_dev, const uint8_t *buffer);

#endif
