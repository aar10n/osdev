//
// Created by Aaron Gill-Braun on 2025-07-27.
//

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <osdev/input.h>
#include <osdev/input-event-codes.h>

// map key codes to characters
const char *keymap[][2] = {
  // modifiers (not printable, handled separately)
  [KEY_LCTRL] = {NULL, NULL},
  [KEY_LSHIFT] = {NULL, NULL},
  [KEY_LALT] = {NULL, NULL},
  [KEY_LMETA] = {NULL, NULL},
  [KEY_RCTRL] = {NULL, NULL},
  [KEY_RSHIFT] = {NULL, NULL},
  [KEY_RALT] = {NULL, NULL},
  [KEY_RMETA] = {NULL, NULL},
  // letters
  [KEY_A] = {"a", "A"},
  [KEY_B] = {"b", "B"},
  [KEY_C] = {"c", "C"},
  [KEY_D] = {"d", "D"},
  [KEY_E] = {"e", "E"},
  [KEY_F] = {"f", "F"},
  [KEY_G] = {"g", "G"},
  [KEY_H] = {"h", "H"},
  [KEY_I] = {"i", "I"},
  [KEY_J] = {"j", "J"},
  [KEY_K] = {"k", "K"},
  [KEY_L] = {"l", "L"},
  [KEY_M] = {"m", "M"},
  [KEY_N] = {"n", "N"},
  [KEY_O] = {"o", "O"},
  [KEY_P] = {"p", "P"},
  [KEY_Q] = {"q", "Q"},
  [KEY_R] = {"r", "R"},
  [KEY_S] = {"s", "S"},
  [KEY_T] = {"t", "T"},
  [KEY_U] = {"u", "U"},
  [KEY_V] = {"v", "V"},
  [KEY_W] = {"w", "W"},
  [KEY_X] = {"x", "X"},
  [KEY_Y] = {"y", "Y"},
  [KEY_Z] = {"z", "Z"},
  // numbers
  [KEY_1] = {"1", "!"},
  [KEY_2] = {"2", "@"},
  [KEY_3] = {"3", "#"},
  [KEY_4] = {"4", "$"},
  [KEY_5] = {"5", "%"},
  [KEY_6] = {"6", "^"},
  [KEY_7] = {"7", "&"},
  [KEY_8] = {"8", "*"},
  [KEY_9] = {"9", "("},
  [KEY_0] = {"0", ")"},
  // special
  [KEY_MINUS] = {"-", "_"},
  [KEY_EQUAL] = {"=", "+"},
  [KEY_LSQUARE] = {"[", "{"},
  [KEY_RSQUARE] = {"]", "}"},
  [KEY_BACKSLASH] = {"\\", "|"},
  [KEY_SEMICOLON] = {";", ":"},
  [KEY_APOSTROPHE] = {"'", "\""},
  [KEY_GRAVE] = {"`", "~"},
  [KEY_COMMA] = {",", "<"},
  [KEY_PERIOD] = {".", ">"},
  [KEY_SLASH] = {"/", "?"},
  // other
  [KEY_SPACE] = {" ", " "},
};

// map key codes to names for special keys
const char *keynames[] = {
  // modifiers
  [KEY_LCTRL] = "KEY_LCTRL",
  [KEY_LSHIFT] = "KEY_LSHIFT",
  [KEY_LALT] = "KEY_LALT",
  [KEY_LMETA] = "KEY_LMETA",
  [KEY_RCTRL] = "KEY_RCTRL",
  [KEY_RSHIFT] = "KEY_RSHIFT",
  [KEY_RALT] = "KEY_RALT",
  [KEY_RMETA] = "KEY_RMETA",
  // function keys
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
  // other
  [KEY_ENTER] = "KEY_ENTER",
  [KEY_ESCAPE] = "KEY_ESCAPE",
  [KEY_BACKSPACE] = "KEY_BACKSPACE",
  [KEY_TAB] = "KEY_TAB",
  [KEY_CAPSLOCK] = "KEY_CAPSLOCK",
  // arrow keys
  [KEY_RIGHT] = "KEY_RIGHT",
  [KEY_LEFT] = "KEY_LEFT",
  [KEY_DOWN] = "KEY_DOWN",
  [KEY_UP] = "KEY_UP",
  // media keys
  [KEY_PRINTSCR] = "KEY_PRINTSCR",
  [KEY_SCROLL_LOCK] = "KEY_SCROLL_LOCK",
  [KEY_PAUSE] = "KEY_PAUSE",
  [KEY_INSERT] = "KEY_INSERT",
  [KEY_HOME] = "KEY_HOME",
  [KEY_END] = "KEY_END",
  [KEY_PAGE_UP] = "KEY_PAGE_UP",
  [KEY_PAGE_DOWN] = "KEY_PAGE_DOWN",
  [KEY_DELETE] = "KEY_DELETE",
};

bool shift_held = false;
bool caps_on = false;

int main(int argc, char **argv) {
  bool raw_mode = false;
  const char *dev = "/dev/events0";

  // parse arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-r") == 0) {
      raw_mode = true;
    } else {
      dev = argv[i];
    }
  }

  int fd = open(dev, O_RDONLY);

  if (fd < 0) {
    perror(dev);
    return 1;
  }

  struct input_event ev;

  while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
    if (ev.type == EV_KEY) {
      // track modifier states
      if (ev.code == KEY_LSHIFT || ev.code == KEY_RSHIFT) {
        shift_held = (ev.value != 0);
      } else if (ev.code == KEY_CAPSLOCK && ev.value == 1) {
        caps_on = !caps_on;
      }

      if (ev.value == 0 || ev.value == 2) continue; // only show press events

      // raw mode - just print keycode
      if (raw_mode) {
        printf("key: %d\n", ev.code);
        continue;
      }

      // check if printable
      if (ev.code < sizeof(keymap)/sizeof(keymap[0]) && keymap[ev.code][0]) {
        bool use_shift = shift_held;
        // letters are affected by caps lock
        if (ev.code >= KEY_A && ev.code <= KEY_Z) {
          use_shift = shift_held ^ caps_on;
        }
        printf("key: %s\n", keymap[ev.code][use_shift ? 1 : 0]);
      }
        // check if special key
      else if (ev.code < sizeof(keynames)/sizeof(keynames[0]) && keynames[ev.code]) {
        printf("key: %s\n", keynames[ev.code]);
      }
        // unknown key
      else {
        printf("key: KEY_%d\n", ev.code);
      }
    }
  }

  close(fd);
  return 0;
}
