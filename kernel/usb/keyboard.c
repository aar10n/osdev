//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#include <usb/keyboard.h>
#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/hid-usage.h>

#include <mm.h>
#include <event.h>
#include <string.h>
#include <printf.h>

key_code_t hid_keyboard_layout[] = {
  [HID_KEYBOARD_A] = VK_KEYCODE_A,
  [HID_KEYBOARD_B] = VK_KEYCODE_B,
  [HID_KEYBOARD_C] = VK_KEYCODE_C,
  [HID_KEYBOARD_D] = VK_KEYCODE_D,
  [HID_KEYBOARD_E] = VK_KEYCODE_E,
  [HID_KEYBOARD_F] = VK_KEYCODE_F,
  [HID_KEYBOARD_G] = VK_KEYCODE_G,
  [HID_KEYBOARD_H] = VK_KEYCODE_H,
  [HID_KEYBOARD_I] = VK_KEYCODE_I,
  [HID_KEYBOARD_J] = VK_KEYCODE_J,
  [HID_KEYBOARD_K] = VK_KEYCODE_K,
  [HID_KEYBOARD_L] = VK_KEYCODE_L,
  [HID_KEYBOARD_M] = VK_KEYCODE_M,
  [HID_KEYBOARD_N] = VK_KEYCODE_N,
  [HID_KEYBOARD_O] = VK_KEYCODE_O,
  [HID_KEYBOARD_P] = VK_KEYCODE_P,
  [HID_KEYBOARD_Q] = VK_KEYCODE_Q,
  [HID_KEYBOARD_R] = VK_KEYCODE_R,
  [HID_KEYBOARD_S] = VK_KEYCODE_S,
  [HID_KEYBOARD_T] = VK_KEYCODE_T,
  [HID_KEYBOARD_U] = VK_KEYCODE_U,
  [HID_KEYBOARD_V] = VK_KEYCODE_V,
  [HID_KEYBOARD_W] = VK_KEYCODE_W,
  [HID_KEYBOARD_X] = VK_KEYCODE_X,
  [HID_KEYBOARD_Y] = VK_KEYCODE_Y,
  [HID_KEYBOARD_Z] = VK_KEYCODE_Z,
  [HID_KEYBOARD_1] = VK_KEYCODE_1,
  [HID_KEYBOARD_2] = VK_KEYCODE_2,
  [HID_KEYBOARD_3] = VK_KEYCODE_3,
  [HID_KEYBOARD_4] = VK_KEYCODE_4,
  [HID_KEYBOARD_5] = VK_KEYCODE_5,
  [HID_KEYBOARD_6] = VK_KEYCODE_6,
  [HID_KEYBOARD_7] = VK_KEYCODE_7,
  [HID_KEYBOARD_8] = VK_KEYCODE_8,
  [HID_KEYBOARD_9] = VK_KEYCODE_9,
  [HID_KEYBOARD_0] = VK_KEYCODE_0,
  [HID_KEYBOARD_RETURN] = VK_KEYCODE_RETURN,
  [HID_KEYBOARD_ESCAPE] = VK_KEYCODE_ESCAPE,
  [HID_KEYBOARD_DELETE] = VK_KEYCODE_DELETE,
  [HID_KEYBOARD_TAB] = VK_KEYCODE_TAB,
  [HID_KEYBOARD_SPACE] = VK_KEYCODE_SPACE,
  [HID_KEYBOARD_MINUS] = VK_KEYCODE_MINUS,
  [HID_KEYBOARD_EQUAL] = VK_KEYCODE_EQUAL,
  [HID_KEYBOARD_LSQUARE] = VK_KEYCODE_LSQUARE,
  [HID_KEYBOARD_RSQUARE] = VK_KEYCODE_RSQUARE,
  [HID_KEYBOARD_BACKSLASH] = VK_KEYCODE_BACKSLASH,
  [HID_KEYBOARD_SEMICOLON] = VK_KEYCODE_SEMICOLON,
  [HID_KEYBOARD_APOSTROPHE] = VK_KEYCODE_APOSTROPHE,
  [HID_KEYBOARD_TILDE] = VK_KEYCODE_TILDE,
  [HID_KEYBOARD_COMMA] = VK_KEYCODE_COMMA,
  [HID_KEYBOARD_PERIOD] = VK_KEYCODE_PERIOD,
  [HID_KEYBOARD_SLASH] = VK_KEYCODE_SLASH,
  [HID_KEYBOARD_CAPSLOCK] = VK_KEYCODE_CAPSLOCK,
  [HID_KEYBOARD_F1] = VK_KEYCODE_F1,
  [HID_KEYBOARD_F2] = VK_KEYCODE_F2,
  [HID_KEYBOARD_F3] = VK_KEYCODE_F3,
  [HID_KEYBOARD_F4] = VK_KEYCODE_F4,
  [HID_KEYBOARD_F5] = VK_KEYCODE_F5,
  [HID_KEYBOARD_F6] = VK_KEYCODE_F6,
  [HID_KEYBOARD_F7] = VK_KEYCODE_F7,
  [HID_KEYBOARD_F8] = VK_KEYCODE_F8,
  [HID_KEYBOARD_F9] = VK_KEYCODE_F9,
  [HID_KEYBOARD_F10] = VK_KEYCODE_F10,
  [HID_KEYBOARD_F11] = VK_KEYCODE_F11,
  [HID_KEYBOARD_F12] = VK_KEYCODE_F12,
  [HID_KEYBOARD_PRINTSCR] = VK_KEYCODE_PRINTSCR,
  [HID_KEYBOARD_SCROLL_LOCK] = VK_KEYCODE_SCROLL_LOCK,
  [HID_KEYBOARD_PAUSE] = VK_KEYCODE_PAUSE,
  [HID_KEYBOARD_INSERT] = VK_KEYCODE_INSERT,
  [HID_KEYBOARD_HOME] = VK_KEYCODE_HOME,
  [HID_KEYBOARD_PAGE_UP] = VK_KEYCODE_PAGE_UP,
  [HID_KEYBOARD_DELETE_FWD] = VK_KEYCODE_DELETE_FWD,
  [HID_KEYBOARD_END] = VK_KEYCODE_END,
  [HID_KEYBOARD_PAGE_DOWN] = VK_KEYCODE_PAGE_DOWN,
  [HID_KEYBOARD_RIGHT] = VK_KEYCODE_RIGHT,
  [HID_KEYBOARD_LEFT] = VK_KEYCODE_LEFT,
  [HID_KEYBOARD_DOWN] = VK_KEYCODE_DOWN,
  [HID_KEYBOARD_UP] = VK_KEYCODE_UP
};

static inline key_event_t *make_key_event(key_code_t code, uint8_t modifiers, bool released) {
  key_event_t *event = kmalloc(sizeof(key_event_t));
  memset(event, 0, sizeof(key_event_t));
  event->modifiers = modifiers;
  event->key_code = code;
  event->release = released;
  event->next = NULL;
  return event;
}

key_code_t hid_keyboard_get_key(uint8_t key) {
  return hid_keyboard_layout[key];
}

hid_keyboard_t *hid_keyboard_init(report_format_t *format) {
  collection_node_t *collection = (void *) format->root->children;
  if (collection->children == NULL ||
      !(collection->usage_page == GENERIC_DESKTOP_PAGE && collection->usage == KEYBOARD_USAGE)){
    return NULL;
  }

  uint8_t bit_offset = 0;
  uint8_t offset = 0;
  hid_keyboard_t *keyboard = kmalloc(sizeof(hid_keyboard_t));
  memset(keyboard, 0, sizeof(hid_keyboard_t));

  base_node_t *node = collection->children;
  while (node != NULL) {
    if (node->type == ITEM_NODE) {
      item_node_t *n = (void *) node;
      if (is_usage_range(n, KEYBOARD_PAGE, HID_KEYBOARD_A, HID_KEYBOARD_F12)) {
        keyboard->buffer_offset = offset;
        keyboard->buffer_size = n->report_count;
      } else if (is_usage_range(n, KEYBOARD_PAGE, HID_KEYBOARD_LCONTROL, HID_KEYBOARD_RGUI)) {
        keyboard->modifier_offset = offset;
      } else if (n->usage_page == LED_PAGE) {
        keyboard->led_offset = offset;
      }

      bit_offset += get_item_size_bits(n);
      if (bit_offset % 8 == 0) {
        offset += bit_offset / 8;
        bit_offset = 0;
      }
    }
    node = node->next;
  }

  keyboard->prev_buffer = kmalloc(offset);
  memset(keyboard->prev_buffer, 0, offset);
  return keyboard;
}

void hid_keyboard_handle_input(hid_device_t *device, uint8_t *buffer) {
  hid_keyboard_t *kb = device->data;
  uint32_t modifiers = buffer[kb->modifier_offset];

  uint8_t *curr = buffer;
  uint8_t *prev = kb->prev_buffer;
  uint8_t char_idx = kb->buffer_offset;
  uint8_t char_max = char_idx + kb->buffer_size;

  for (int i = char_idx; i < char_max; i++) {
    // check for pressed keys
    if (curr[i] != 0) {
      // a key is pressed
      for (int j = char_idx; j < char_max; j++) {
        if (curr[i] == prev[j]) {
          // key was held down, dont emit an event
          goto outer;
        }
      }
      // create key press event
      key_code_t code = hid_keyboard_layout[curr[i]];
      key_event_t *event = make_key_event(code, modifiers, false);
      dispatch_key_event(event);
      continue; // check next byte
    } else {
      // check for released keys
      for (int j = char_idx; j < char_max; j++) {
        if (prev[j] != 0) {
          // create key release event
          key_code_t code = hid_keyboard_layout[prev[i]];
          key_event_t *event = make_key_event(code, modifiers, true);
          dispatch_key_event(event);
        }
      }
      break; // no more bytes changed
    }

  LABEL(outer);
  }

  memcpy(kb->prev_buffer, buffer, device->size);
}
