//
// Created by Aaron Gill-Braun on 2022-12-09.
//

#include <kernel/input.h>
#include <bitmap.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

static const char *input_code_to_name[] = {
  [BTN_MOUSE1] = "BTN_MOUSE1",
  [BTN_MOUSE2] = "BTN_MOUSE2",
  [BTN_MOUSE3] = "BTN_MOUSE3",

  [KEY_LCTRL] = "KEY_LCTRL",
  [KEY_LSHIFT] = "KEY_LSHIFT",
  [KEY_LALT] = "KEY_LALT",
  [KEY_LMETA] = "KEY_LMETA",
  [KEY_RCTRL] = "KEY_RCTRL",
  [KEY_RSHIFT] = "KEY_RSHIFT",
  [KEY_RALT] = "KEY_RALT",
  [KEY_RMETA] = "KEY_RMETA",

  [KEY_A] = "KEY_A",
  [KEY_B] = "KEY_B",
  [KEY_C] = "KEY_C",
  [KEY_D] = "KEY_D",
  [KEY_E] = "KEY_E",
  [KEY_F] = "KEY_F",
  [KEY_G] = "KEY_G",
  [KEY_H] = "KEY_H",
  [KEY_I] = "KEY_I",
  [KEY_J] = "KEY_J",
  [KEY_K] = "KEY_K",
  [KEY_L] = "KEY_L",
  [KEY_M] = "KEY_M",
  [KEY_N] = "KEY_N",
  [KEY_O] = "KEY_O",
  [KEY_P] = "KEY_P",
  [KEY_Q] = "KEY_Q",
  [KEY_R] = "KEY_R",
  [KEY_S] = "KEY_S",
  [KEY_T] = "KEY_T",
  [KEY_U] = "KEY_U",
  [KEY_V] = "KEY_V",
  [KEY_W] = "KEY_W",
  [KEY_X] = "KEY_X",
  [KEY_Y] = "KEY_Y",
  [KEY_Z] = "KEY_Z",

  [KEY_1] = "KEY_1",
  [KEY_2] = "KEY_2",
  [KEY_3] = "KEY_3",
  [KEY_4] = "KEY_4",
  [KEY_5] = "KEY_5",
  [KEY_6] = "KEY_6",
  [KEY_7] = "KEY_7",
  [KEY_8] = "KEY_8",
  [KEY_9] = "KEY_9",
  [KEY_0] = "KEY_0",

  [KEY_F1] = "KEY_F1",
  [KEY_F2] = "KEY_F2",
  [KEY_F3] = "KEY_F3",
  [KEY_F4] = "KEY_F4",
  [KEY_F5] = "KEY_F5",
  [KEY_F6] = "KEY_F6",
  [KEY_F7] = "KEY_F7",
  [KEY_F8] = "KEY_F8",
  [KEY_F9] = "KEY_F9",
  [KEY_F10] = "KEY_F10",
  [KEY_F11] = "KEY_F11",
  [KEY_F12] = "KEY_F12",

  [KEY_RETURN] = "KEY_RETURN",
  [KEY_ESCAPE] = "KEY_ESCAPE",
  [KEY_DELETE] = "KEY_DELETE",
  [KEY_TAB] = "KEY_TAB",
  [KEY_SPACE] = "KEY_SPACE",
  [KEY_CAPSLOCK] = "KEY_CAPSLOCK",

  [KEY_MINUS] = "KEY_MINUS",
  [KEY_EQUAL] = "KEY_EQUAL",
  [KEY_LSQUARE] = "KEY_LSQUARE",
  [KEY_RSQUARE] = "KEY_RSQUARE",
  [KEY_BACKSLASH] = "KEY_BACKSLASH",
  [KEY_SEMICOLON] = "KEY_SEMICOLON",
  [KEY_APOSTROPHE] = "KEY_APOSTROPHE",
  [KEY_TILDE] = "KEY_TILDE",
  [KEY_COMMA] = "KEY_COMMA",
  [KEY_PERIOD] = "KEY_PERIOD",
  [KEY_SLASH] = "KEY_SLASH",

  [KEY_RIGHT] = "KEY_RIGHT",
  [KEY_LEFT] = "KEY_LEFT",
  [KEY_DOWN] = "KEY_DOWN",
  [KEY_UP] = "KEY_UP",

  [KEY_PRINTSCR] = "KEY_PRINTSCR",
  [KEY_SCROLL_LOCK] = "KEY_SCROLL_LOCK",
  [KEY_PAUSE] = "KEY_PAUSE",
  [KEY_INSERT] = "KEY_INSERT",
  [KEY_HOME] = "KEY_HOME",
  [KEY_END] = "KEY_END",
  [KEY_PAGE_UP] = "KEY_PAGE_UP",
  [KEY_PAGE_DOWN] = "KEY_PAGE_DOWN",
  [KEY_DELETE_FWD] = "KEY_DELETE_FWD",
};

static inline char key_to_char_lower(uint16_t key) {
  switch (key) {
    case KEY_A: return 'a';
    case KEY_B: return 'b';
    case KEY_C: return 'c';
    case KEY_D: return 'd';
    case KEY_E: return 'e';
    case KEY_F: return 'f';
    case KEY_G: return 'g';
    case KEY_H: return 'h';
    case KEY_I: return 'i';
    case KEY_J: return 'j';
    case KEY_K: return 'k';
    case KEY_L: return 'l';
    case KEY_M: return 'm';
    case KEY_N: return 'n';
    case KEY_O: return 'o';
    case KEY_P: return 'p';
    case KEY_Q: return 'q';
    case KEY_R: return 'r';
    case KEY_S: return 's';
    case KEY_T: return 't';
    case KEY_U: return 'u';
    case KEY_V: return 'v';
    case KEY_W: return 'w';
    case KEY_X: return 'x';
    case KEY_Y: return 'y';
    case KEY_Z: return 'z';

    case KEY_1: return '1';
    case KEY_2: return '2';
    case KEY_3: return '3';
    case KEY_4: return '4';
    case KEY_5: return '5';
    case KEY_6: return '6';
    case KEY_7: return '7';
    case KEY_8: return '8';
    case KEY_9: return '9';
    case KEY_0: return '0';

    case KEY_RETURN: return '\n';
    case KEY_DELETE: return '\b';
    case KEY_TAB: return '\t';
    case KEY_SPACE: return ' ';

    case KEY_MINUS: return '-';
    case KEY_EQUAL: return '=';
    case KEY_LSQUARE: return '[';
    case KEY_RSQUARE: return ']';
    case KEY_BACKSLASH: return '\\';
    case KEY_SEMICOLON: return ';';
    case KEY_APOSTROPHE: return '\'';
    case KEY_TILDE: return '`';
    case KEY_COMMA: return ',';
    case KEY_PERIOD: return '.';
    case KEY_SLASH: return '/';
    default: return 0;
  }
}

static inline char key_to_char_upper(uint16_t key) {
  switch (key) {
    case KEY_A: return 'A';
    case KEY_B: return 'B';
    case KEY_C: return 'C';
    case KEY_D: return 'D';
    case KEY_E: return 'E';
    case KEY_F: return 'F';
    case KEY_G: return 'G';
    case KEY_H: return 'H';
    case KEY_I: return 'I';
    case KEY_J: return 'J';
    case KEY_K: return 'K';
    case KEY_L: return 'L';
    case KEY_M: return 'M';
    case KEY_N: return 'N';
    case KEY_O: return 'O';
    case KEY_P: return 'P';
    case KEY_Q: return 'Q';
    case KEY_R: return 'R';
    case KEY_S: return 'S';
    case KEY_T: return 'T';
    case KEY_U: return 'U';
    case KEY_V: return 'V';
    case KEY_W: return 'W';
    case KEY_X: return 'X';
    case KEY_Y: return 'Y';
    case KEY_Z: return 'Z';

    case KEY_1: return '!';
    case KEY_2: return '@';
    case KEY_3: return '#';
    case KEY_4: return '$';
    case KEY_5: return '%';
    case KEY_6: return '^';
    case KEY_7: return '&';
    case KEY_8: return '*';
    case KEY_9: return '(';
    case KEY_0: return ')';

    case KEY_RETURN: return '\n';
    case KEY_DELETE: return '\b';
    case KEY_TAB: return '\t';
    case KEY_SPACE: return ' ';

    case KEY_MINUS: return '_';
    case KEY_EQUAL: return '+';
    case KEY_LSQUARE: return '{';
    case KEY_RSQUARE: return '}';
    case KEY_BACKSLASH: return '|';
    case KEY_SEMICOLON: return ':';
    case KEY_APOSTROPHE: return '"';
    case KEY_TILDE: return '~';
    case KEY_COMMA: return '<';
    case KEY_PERIOD: return '>';
    case KEY_SLASH: return '?';
    default: return 0;
  }
}

static inline uint8_t key_to_modifier_bit(uint16_t key) {
  switch (key) {
    case KEY_LCTRL:
    case KEY_RCTRL:
      return MOD_CTRL;
    case KEY_LSHIFT:
    case KEY_RSHIFT:
      return MOD_SHIFT;
    case KEY_LALT:
    case KEY_RALT:
      return MOD_ALT;
    case KEY_LMETA:
    case KEY_RMETA:
      return MOD_META;
    case KEY_CAPSLOCK:
      return MOD_CAPS;
    default:
      return 0;
  }
}

static bitmap_t *key_states;
static uint8_t key_modifiers;
chan_t *key_event_stream;

//

static int input_process_key_event(uint16_t flags, uint32_t value) {
  uint16_t key = KEY_CODE(value);
  uint8_t bit = key_to_modifier_bit(key);
  bool pressed = KEY_STATE(value);
  if (key >= KEY_MAX) {
    return -1;
  }

  kprintf("input: key event %s (%d) [pressed = %d]\n", input_code_to_name[key], key, pressed);

  // update keymap state
  if (pressed) {
    bitmap_set(key_states, key);
    if (bit)
      key_modifiers |= bit;
  } else {
    bitmap_clear(key_states, key);
    if (bit)
      key_modifiers &= ~bit; // clear modifier
  }

  if (bit) {
    // update modifier state
    if (pressed) {
      key_modifiers |= bit;
    } else {
      key_modifiers &= ~bit;
    }
  }

  if (pressed) {
    // format and send key event to stream
    input_key_event_t event = { .key = key, .modifiers = key_modifiers };
    chan_send(key_event_stream, event.raw);
  }
  return 0;
}

//

int input_event(ev_type_t type, uint16_t flags, uint32_t value) {
  switch (type) {
    case EV_KEY:
      return input_process_key_event(flags, value);
    case EV_MOUSE:
      break;
    default:
      unreachable;
  }
  return 0;
}

int input_getkey(uint16_t key) {
  if (key >= KEY_MAX) {
    return 0;
  }
  return bitmap_get(key_states, key);
}

int input_key_event_to_char(input_key_event_t *event) {
  kassert(event);
  if (event->modifiers & MOD_SHIFT || event->modifiers & MOD_CAPS) {
    return key_to_char_upper(event->key);
  }
  return key_to_char_lower(event->key);
}

//

void input_init() {
  key_states = create_bitmap(KEY_MAX + 1);
  key_event_stream = chan_alloc(128, 0);
}
MODULE_INIT(input_init);
