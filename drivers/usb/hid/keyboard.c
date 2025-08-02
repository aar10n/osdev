//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#include "keyboard.h"
#include "hid-usage.h"

#include <kernel/mm.h>
#include <kernel/input.h>
#include <kernel/string.h>

#include <asm/bits.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("keyboard: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("keyboard: %s: " x, __func__, ##__VA_ARGS__)

uint16_t hid_keyboard_to_input_key[] = {
  [HID_KEYBOARD_A] = KEY_A,
  [HID_KEYBOARD_B] = KEY_B,
  [HID_KEYBOARD_C] = KEY_C,
  [HID_KEYBOARD_D] = KEY_D,
  [HID_KEYBOARD_E] = KEY_E,
  [HID_KEYBOARD_F] = KEY_F,
  [HID_KEYBOARD_G] = KEY_G,
  [HID_KEYBOARD_H] = KEY_H,
  [HID_KEYBOARD_I] = KEY_I,
  [HID_KEYBOARD_J] = KEY_J,
  [HID_KEYBOARD_K] = KEY_K,
  [HID_KEYBOARD_L] = KEY_L,
  [HID_KEYBOARD_M] = KEY_M,
  [HID_KEYBOARD_N] = KEY_N,
  [HID_KEYBOARD_O] = KEY_O,
  [HID_KEYBOARD_P] = KEY_P,
  [HID_KEYBOARD_Q] = KEY_Q,
  [HID_KEYBOARD_R] = KEY_R,
  [HID_KEYBOARD_S] = KEY_S,
  [HID_KEYBOARD_T] = KEY_T,
  [HID_KEYBOARD_U] = KEY_U,
  [HID_KEYBOARD_V] = KEY_V,
  [HID_KEYBOARD_W] = KEY_W,
  [HID_KEYBOARD_X] = KEY_X,
  [HID_KEYBOARD_Y] = KEY_Y,
  [HID_KEYBOARD_Z] = KEY_Z,
  [HID_KEYBOARD_1] = KEY_1,
  [HID_KEYBOARD_2] = KEY_2,
  [HID_KEYBOARD_3] = KEY_3,
  [HID_KEYBOARD_4] = KEY_4,
  [HID_KEYBOARD_5] = KEY_5,
  [HID_KEYBOARD_6] = KEY_6,
  [HID_KEYBOARD_7] = KEY_7,
  [HID_KEYBOARD_8] = KEY_8,
  [HID_KEYBOARD_9] = KEY_9,
  [HID_KEYBOARD_0] = KEY_0,
  [HID_KEYBOARD_RETURN] = KEY_ENTER,
  [HID_KEYBOARD_ESCAPE] = KEY_ESCAPE,
  [HID_KEYBOARD_DELETE] = KEY_BACKSPACE,
  [HID_KEYBOARD_TAB] = KEY_TAB,
  [HID_KEYBOARD_SPACE] = KEY_SPACE,
  [HID_KEYBOARD_MINUS] = KEY_MINUS,
  [HID_KEYBOARD_EQUAL] = KEY_EQUAL,
  [HID_KEYBOARD_LSQUARE] = KEY_LSQUARE,
  [HID_KEYBOARD_RSQUARE] = KEY_RSQUARE,
  [HID_KEYBOARD_BACKSLASH] = KEY_BACKSLASH,
  [HID_KEYBOARD_SEMICOLON] = KEY_SEMICOLON,
  [HID_KEYBOARD_APOSTROPHE] = KEY_APOSTROPHE,
  [HID_KEYBOARD_TILDE] = KEY_GRAVE,
  [HID_KEYBOARD_COMMA] = KEY_COMMA,
  [HID_KEYBOARD_PERIOD] = KEY_PERIOD,
  [HID_KEYBOARD_SLASH] = KEY_SLASH,
  [HID_KEYBOARD_CAPSLOCK] = KEY_CAPSLOCK,
  [HID_KEYBOARD_F1] = KEY_F1,
  [HID_KEYBOARD_F2] = KEY_F2,
  [HID_KEYBOARD_F3] = KEY_F3,
  [HID_KEYBOARD_F4] = KEY_F4,
  [HID_KEYBOARD_F5] = KEY_F5,
  [HID_KEYBOARD_F6] = KEY_F6,
  [HID_KEYBOARD_F7] = KEY_F7,
  [HID_KEYBOARD_F8] = KEY_F8,
  [HID_KEYBOARD_F9] = KEY_F9,
  [HID_KEYBOARD_F10] = KEY_F10,
  [HID_KEYBOARD_F11] = KEY_F11,
  [HID_KEYBOARD_F12] = KEY_F12,
  [HID_KEYBOARD_PRINTSCR] = KEY_PRINTSCR,
  [HID_KEYBOARD_SCROLL_LOCK] = KEY_SCROLL_LOCK,
  [HID_KEYBOARD_PAUSE] = KEY_PAUSE,
  [HID_KEYBOARD_INSERT] = KEY_INSERT,
  [HID_KEYBOARD_HOME] = KEY_HOME,
  [HID_KEYBOARD_PAGE_UP] = KEY_PAGE_UP,
  [HID_KEYBOARD_DELETE_FWD] = KEY_DELETE,
  [HID_KEYBOARD_END] = KEY_END,
  [HID_KEYBOARD_PAGE_DOWN] = KEY_PAGE_DOWN,
  [HID_KEYBOARD_RIGHT] = KEY_RIGHT,
  [HID_KEYBOARD_LEFT] = KEY_LEFT,
  [HID_KEYBOARD_DOWN] = KEY_DOWN,
  [HID_KEYBOARD_UP] = KEY_UP,
  [HID_KEYBOARD_NUM_LOCK] = KEY_NUM_LOCK,
  [HID_KEYBOARD_KP_SLASH] = KEY_KP_SLASH,
  [HID_KEYBOARD_KP_ASTERISK] = KEY_KP_ASTERISK,
  [HID_KEYBOARD_KP_MINUS] = KEY_KP_MINUS,
  [HID_KEYBOARD_KP_PLUS] = KEY_KP_PLUS,
  [HID_KEYBOARD_KP_ENTER] = KEY_KP_ENTER,
  [HID_KEYBOARD_KP_1] = KEY_KP_1,
  [HID_KEYBOARD_KP_2] = KEY_KP_2,
  [HID_KEYBOARD_KP_3] = KEY_KP_3,
  [HID_KEYBOARD_KP_4] = KEY_KP_4,
  [HID_KEYBOARD_KP_5] = KEY_KP_5,
  [HID_KEYBOARD_KP_6] = KEY_KP_6,
  [HID_KEYBOARD_KP_7] = KEY_KP_7,
  [HID_KEYBOARD_KP_8] = KEY_KP_8,
  [HID_KEYBOARD_KP_9] = KEY_KP_9,
  [HID_KEYBOARD_KP_0] = KEY_KP_0,
  [HID_KEYBOARD_KP_PERIOD] = KEY_KP_PERIOD,
  [HID_KEYBOARD_NON_US_BACKSLASH] = KEY_BACKSLASH, // map to regular backslash for now
  [HID_KEYBOARD_APPLICATION] = KEY_APPLICATION,
  [HID_KEYBOARD_POWER] = KEY_POWER,
  [HID_KEYBOARD_KP_EQUAL] = KEY_KP_EQUAL,
  [HID_KEYBOARD_F13] = KEY_F13,
  [HID_KEYBOARD_F14] = KEY_F14,
  [HID_KEYBOARD_F15] = KEY_F15,
  [HID_KEYBOARD_F16] = KEY_F16,
  [HID_KEYBOARD_F17] = KEY_F17,
  [HID_KEYBOARD_F18] = KEY_F18,
  [HID_KEYBOARD_F19] = KEY_F19,
  [HID_KEYBOARD_F20] = KEY_F20,
  [HID_KEYBOARD_F21] = KEY_F21,
  [HID_KEYBOARD_F22] = KEY_F22,
  [HID_KEYBOARD_F23] = KEY_F23,
  [HID_KEYBOARD_F24] = KEY_F24,
  [HID_KEYBOARD_EXECUTE] = KEY_EXECUTE,
  [HID_KEYBOARD_HELP] = KEY_HELP,
  [HID_KEYBOARD_MENU] = KEY_MENU,
  [HID_KEYBOARD_SELECT] = KEY_SELECT,
  [HID_KEYBOARD_STOP] = KEY_STOP,
  [HID_KEYBOARD_AGAIN] = KEY_AGAIN,
  [HID_KEYBOARD_UNDO] = KEY_UNDO,
  [HID_KEYBOARD_CUT] = KEY_CUT,
  [HID_KEYBOARD_COPY] = KEY_COPY,
  [HID_KEYBOARD_PASTE] = KEY_PASTE,
  [HID_KEYBOARD_FIND] = KEY_FIND,
  [HID_KEYBOARD_MUTE] = KEY_MUTE,
  [HID_KEYBOARD_VOLUME_UP] = KEY_VOLUME_UP,
  [HID_KEYBOARD_VOLUME_DOWN] = KEY_VOLUME_DOWN
};

uint16_t hid_modifier_bit_to_input_key[] = {
  [HID_BIT_LCONTROL] = KEY_LCTRL,
  [HID_BIT_LSHIFT] = KEY_LSHIFT,
  [HID_BIT_LALT] = KEY_LALT,
  [HID_BIT_LSPECIAL] = KEY_LMETA,
  [HID_BIT_RCONTROL] = KEY_RCTRL,
  [HID_BIT_RSHIFT] = KEY_RSHIFT,
  [HID_BIT_RALT] = KEY_RALT,
  [HID_BIT_RSPECIAL] = KEY_RMETA,
};

static inline bool u8_in_buffer(const uint8_t *buffer, size_t len, uint8_t val) {
  for (int i = 0; i < len; i++) {
    if (buffer[i] == val)
      return true;
  }
  return false;
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

void hid_keyboard_handle_input(hid_device_t *hid_dev, uint8_t *buffer) {
  hid_keyboard_t *kb = hid_dev->data;
  uint8_t char_idx = kb->buffer_offset;
  uint8_t char_max = char_idx + kb->buffer_size;
  size_t len = kb->buffer_size - kb->buffer_offset;

  uint8_t prev_mod = kb->prev_buffer[kb->modifier_offset];
  uint8_t curr_mod = buffer[kb->modifier_offset];
  uint8_t *curr = buffer;
  uint8_t *prev = kb->prev_buffer;

  // handle modifiers first
  uint8_t moddiff = curr_mod ^ prev_mod;
  while (moddiff != 0) {
    uint8_t b = __bsf8(moddiff);
    uint8_t mod = 1 << b;
    uint8_t state = (prev_mod & mod) ? 0 : 1;

    // emit the event
    uint16_t key = hid_modifier_bit_to_input_key[b];
    input_event(EV_KEY, key, state);

    moddiff ^= mod;
  }

  // handle key presses
  for (int i = char_idx; i < char_max; i++) {
    uint8_t val = curr[i];
    if (val == 0)
      break;
    // skip if it is also in the previous buffer
    if (u8_in_buffer(prev + char_idx, len, val))
      continue;

    // emit the event
    uint16_t key = hid_keyboard_to_input_key[val];
    input_event(EV_KEY, key, 1);
  }

  // handle key releases
  for (int i = char_idx; i < char_max; i++) {
    uint8_t val = prev[i];
    if (val == 0)
      break;
    // skip if it is in the current buffer
    if (u8_in_buffer(curr + char_idx, len, val))
      continue;

    // emit the event
    uint16_t key = hid_keyboard_to_input_key[val];
    input_event(EV_KEY, key, 0);
  }

  memcpy(kb->prev_buffer, buffer, hid_dev->size);
}
