//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#ifndef KERNEL_EVENT_H
#define KERNEL_EVENT_H

#include <base.h>

// modifiers
#define L_CONTROL (1 << 0)
#define L_SHIFT   (1 << 1)
#define L_ALT     (1 << 2)
#define L_SPECIAL (1 << 3)
#define R_CONTROL (1 << 4)
#define R_SHIFT   (1 << 5)
#define R_ALT     (1 << 6)
#define R_SPECIAL (1 << 7)

typedef enum key_code {
  // letters
  VK_KEYCODE_A,           // a and A
  VK_KEYCODE_B,           // b and B
  VK_KEYCODE_C,           // c and C
  VK_KEYCODE_D,           // d and D
  VK_KEYCODE_E,           // e and E
  VK_KEYCODE_F,           // f and F
  VK_KEYCODE_G,           // g and G
  VK_KEYCODE_H,           // h and H
  VK_KEYCODE_I,           // i and I
  VK_KEYCODE_J,           // j and J
  VK_KEYCODE_K,           // k and K
  VK_KEYCODE_L,           // l and L
  VK_KEYCODE_M,           // m and M
  VK_KEYCODE_N,           // n and N
  VK_KEYCODE_O,           // o and O
  VK_KEYCODE_P,           // p and P
  VK_KEYCODE_Q,           // q and Q
  VK_KEYCODE_R,           // r and R
  VK_KEYCODE_S,           // s and S
  VK_KEYCODE_T,           // t and T
  VK_KEYCODE_U,           // u and U
  VK_KEYCODE_V,           // v and V
  VK_KEYCODE_W,           // w and W
  VK_KEYCODE_X,           // x and X
  VK_KEYCODE_Y,           // y and Y
  VK_KEYCODE_Z,           // z and Z
  // numbers
  VK_KEYCODE_1,           // 1 and !
  VK_KEYCODE_2,           // 2 and @
  VK_KEYCODE_3,           // 3 and #
  VK_KEYCODE_4,           // 4 and $
  VK_KEYCODE_5,           // 5 and %
  VK_KEYCODE_6,           // 6 and ^
  VK_KEYCODE_7,           // 7 and &
  VK_KEYCODE_8,           // 8 and *
  VK_KEYCODE_9,           // 9 and (
  VK_KEYCODE_0,           // 0 and )
  // other
  VK_KEYCODE_RETURN,      // return (enter)
  VK_KEYCODE_ESCAPE,      // escape
  VK_KEYCODE_DELETE,      // delete (backspace)
  VK_KEYCODE_TAB,         // tab
  VK_KEYCODE_SPACE,       // spacebar
  VK_KEYCODE_CAPSLOCK,    // caps lock
  // special
  VK_KEYCODE_MINUS,       // - and _
  VK_KEYCODE_EQUAL,       // = and +
  VK_KEYCODE_LSQUARE,     // [ and {
  VK_KEYCODE_RSQUARE,     // ] and }
  VK_KEYCODE_BACKSLASH,   // \ and |
  VK_KEYCODE_SEMICOLON,   // ; and :
  VK_KEYCODE_APOSTROPHE,  // ' and "
  VK_KEYCODE_TILDE,       // ` and ~
  VK_KEYCODE_COMMA,       // , and <
  VK_KEYCODE_PERIOD,      // . and >
  VK_KEYCODE_SLASH,       // / and ?
  // function keys
  VK_KEYCODE_F1,          // F1
  VK_KEYCODE_F2,          // F2
  VK_KEYCODE_F3,          // F3
  VK_KEYCODE_F4,          // F4
  VK_KEYCODE_F5,          // F5
  VK_KEYCODE_F6,          // F6
  VK_KEYCODE_F7,          // F7
  VK_KEYCODE_F8,          // F8
  VK_KEYCODE_F9,          // F9
  VK_KEYCODE_F10,         // F10
  VK_KEYCODE_F11,         // F11
  VK_KEYCODE_F12,         // F12
  // media keys
  VK_KEYCODE_PRINTSCR,    // print screen
  VK_KEYCODE_SCROLL_LOCK, // scroll lock
  VK_KEYCODE_PAUSE,       // pause
  VK_KEYCODE_INSERT,      // insert
  VK_KEYCODE_HOME,        // home
  VK_KEYCODE_END,         // end
  VK_KEYCODE_PAGE_UP,     // page up
  VK_KEYCODE_PAGE_DOWN,   // page down
  VK_KEYCODE_DELETE_FWD,  // delete forward
  // arrow keys
  VK_KEYCODE_RIGHT,       // right arrow
  VK_KEYCODE_LEFT,        // left arrow
  VK_KEYCODE_DOWN,        // down arrow
  VK_KEYCODE_UP,          // up arrow
} key_code_t;

typedef struct key_event {
  uint8_t modifiers;
  key_code_t key_code;
  bool release;
  struct key_event *next;
} key_event_t;

typedef struct {
  key_event_t *first;
  key_event_t *last;
  size_t count;
} key_event_queue_t;


void events_init();
key_event_t *wait_for_key_event();
void dispatch_key_event(key_event_t *event);
char key_event_to_character(key_event_t *event);

#endif
