//
// Created by Aaron Gill-Braun on 2021-09-11.
//

#include <usb/mouse.h>
#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/hid-usage.h>
#include <printf.h>

uint16_t mouse_x = 0;
uint16_t mouse_y = 0;

hid_mouse_t *hid_mouse_init(report_format_t *format) {
  collection_node_t *collection = (void *) format->root->children;
  if (collection->children == NULL ||
      !(collection->usage_page == GENERIC_DESKTOP_PAGE && collection->usage == MOUSE_USAGE)){
    return NULL;
  }

  uint8_t bit_offset = 0;
  uint8_t offset = 0;
  hid_mouse_t *mouse = kmalloc(sizeof(hid_mouse_t));
  memset(mouse, 0, sizeof(hid_mouse_t));

  base_node_t *node = collection->children;
  while (node != NULL) {
    if (node->type == ITEM_NODE) {
      item_node_t *n = (void *) node;
      if (n->usage_page == BUTTON_PAGE && n->report_size == 1) {
        mouse->buttons_offset = offset;
      } else if (n->usage_page == GENERIC_DESKTOP_PAGE) {
        usage_node_t *x_usage = get_usage(n, GENERIC_DESKTOP_PAGE, X_USAGE, X_USAGE);
        usage_node_t *y_usage = get_usage(n, GENERIC_DESKTOP_PAGE, Y_USAGE, Y_USAGE);
        mouse->x_offset = offset + get_usage_offset(n, x_usage);
        mouse->y_offset = offset + get_usage_offset(n, y_usage);
      }

      bit_offset += get_item_size_bits(n);
      if (bit_offset % 8 == 0) {
        offset += bit_offset / 8;
        bit_offset = 0;
      }
    } else if (node->type == COLLECTION_NODE) {
      collection_node_t *n = (void *) node;
      collection = n;
      node = n->children;
      continue;
    }
    node = node->next;
  }
  return mouse;
}

void hid_mouse_handle_input(hid_device_t *device, const uint8_t *buffer) {
  hid_mouse_t *mouse = device->data;
  uint8_t buttons = buffer[mouse->buttons_offset];
  int8_t pos_x = buffer[mouse->x_offset];
  int8_t pos_y = buffer[mouse->y_offset];

  mouse_x = min(mouse_x + pos_x, boot_info->fb_width);
  mouse_y = min(mouse_y + pos_y, boot_info->fb_height);
  // kprintf("MOUSE\n");
  // kprintf("  buttons: %b\n", buttons);
  // kprintf("  x: %d\n", pos_x);
  // kprintf("  y: %d\n", pos_y);
}
