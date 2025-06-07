//
// Created by Aaron Gill-Braun on 2022-12-09.
//

#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <kernel/base.h>
#include <kernel/chan.h>

typedef enum packed input_evtyp {
  EV_KEY = 1, // keyboard or button event
  EV_MOUSE,   // mouse movement event
} input_evtyp_t;

typedef struct input_event {
  input_evtyp_t type;
  uint16_t flags;
  uint32_t value;
} ev_packet_t;
static_assert(sizeof(ev_packet_t) == sizeof(uint64_t));

#define KEY_VALUE(c, s) (uint32_t)((c) | (((s) & 1) << 16))
#define     KEY_CODE(v) ((v) & UINT16_MAX)
#define     KEY_STATE(v) (((v) >> 16) & 1)
#define MOUSE_VALUE(x, y) (uint32_t)((x) | ((y) << 16))
#define     MOUSE_X(v) ((v) & UINT16_MAX)
#define     MOUSE_Y(v) (((v) >> 16) & UINT16_MAX)

//
// event flags
//

// key events

// mouse events
#define MOUSE_EV_REL (1 << 0)   // mouse event is relative
#define MOUSE_EV_ABS (1 << 1)   // mouse event is absolute

//
// event values
//

// mouse buttons
#define BTN_MOUSE1      0x001 // left mouse button
#define BTN_MOUSE2      0x002 // right mouse button
#define BTN_MOUSE3      0x003 // middle mouse button

// modifiers
#define KEY_LCTRL       0x008 // left control
#define KEY_LSHIFT      0x009 // left shift
#define KEY_LALT        0x00A // left alt
#define KEY_LMETA       0x00B // left special/command
#define KEY_RCTRL       0x00C // right control
#define KEY_RSHIFT      0x00D // right shift
#define KEY_RALT        0x00E // right alt
#define KEY_RMETA       0x00F // right special/command
// letters
#define KEY_A           0x010 // a and A
#define KEY_B           0x012 // b and B
#define KEY_C           0x013 // c and C
#define KEY_D           0x014 // d and D
#define KEY_E           0x015 // e and E
#define KEY_F           0x016 // f and F
#define KEY_G           0x017 // g and G
#define KEY_H           0x018 // h and H
#define KEY_I           0x019 // i and I
#define KEY_J           0x01A // j and J
#define KEY_K           0x01B // k and K
#define KEY_L           0x01C // l and L
#define KEY_M           0x01D // m and M
#define KEY_N           0x01E // n and N
#define KEY_O           0x01F // o and O
#define KEY_P           0x020 // p and P
#define KEY_Q           0x021 // q and Q
#define KEY_R           0x022 // r and R
#define KEY_S           0x023 // s and S
#define KEY_T           0x024 // t and T
#define KEY_U           0x025 // u and U
#define KEY_V           0x026 // v and V
#define KEY_W           0x027 // w and W
#define KEY_X           0x028 // x and X
#define KEY_Y           0x029 // y and Y
#define KEY_Z           0x02A // z and Z
// numbers
#define KEY_1           0x02B // 1 and !
#define KEY_2           0x02C // 2 and @
#define KEY_3           0x02D // 3 and #
#define KEY_4           0x02E // 4 and $
#define KEY_5           0x02F // 5 and %
#define KEY_6           0x030 // 6 and ^
#define KEY_7           0x031 // 7 and &
#define KEY_8           0x032 // 8 and *
#define KEY_9           0x033 // 9 and (
#define KEY_0           0x034 // 0 and )
// function keys
#define KEY_F1          0x035 // F1
#define KEY_F2          0x036 // F2
#define KEY_F3          0x037 // F3
#define KEY_F4          0x038 // F4
#define KEY_F5          0x039 // F5
#define KEY_F6          0x03A // F6
#define KEY_F7          0x03B // F7
#define KEY_F8          0x03C // F8
#define KEY_F9          0x03D // F9
#define KEY_F10         0x03E // F10
#define KEY_F11         0x03F // F11
#define KEY_F12         0x040 // F12
// other
#define KEY_RETURN      0x041 // return (enter)
#define KEY_ESCAPE      0x042 // escape
#define KEY_DELETE      0x043 // delete (backspace)
#define KEY_TAB         0x044 // tab
#define KEY_SPACE       0x045 // spacebar
#define KEY_CAPSLOCK    0x046 // caps lock
// special
#define KEY_MINUS       0x047 // - and _
#define KEY_EQUAL       0x048 // = and +
#define KEY_LSQUARE     0x049 // [ and {
#define KEY_RSQUARE     0x04A // ] and }
#define KEY_BACKSLASH   0x04B // \ and |
#define KEY_SEMICOLON   0x04C // ; and :
#define KEY_APOSTROPHE  0x04D // ' and "
#define KEY_TILDE       0x04E // ` and ~
#define KEY_COMMA       0x04F // , and <
#define KEY_PERIOD      0x050 // . and >
#define KEY_SLASH       0x051 // / and ?
// arrow keys
#define KEY_RIGHT       0x052 // right arrow
#define KEY_LEFT        0x053 // left arrow
#define KEY_DOWN        0x054 // down arrow
#define KEY_UP          0x055 // up arrow
// media keys
#define KEY_PRINTSCR    0x056 // print screen
#define KEY_SCROLL_LOCK 0x057 // scroll lock
#define KEY_PAUSE       0x058 // pause
#define KEY_INSERT      0x059 // insert
#define KEY_HOME        0x05A // home
#define KEY_END         0x05B // end
#define KEY_PAGE_UP     0x05C // page up
#define KEY_PAGE_DOWN   0x05D // page down
#define KEY_DELETE_FWD  0x05E // delete forward

#define KEY_MAX         0x05F

//

typedef union input_key_event {
  struct {
    uint16_t key;
    uint8_t modifiers;
  };
  // to make it easy to send/receive thru chan
  uint64_t raw;
} input_key_event_t;
static_assert(sizeof(input_key_event_t) == sizeof(uint64_t));

#define MOD_CTRL   (1 << 0)
#define MOD_SHIFT  (1 << 1)
#define MOD_ALT    (1 << 2)
#define MOD_META   (1 << 3)
#define MOD_CAPS   (1 << 4)


/**
 * A channel of input_key_event_t objects.
 */
extern chan_t *key_event_stream;

/**
 * Called by input device drivers to notify the kernel of an event.
 *
 * @param type The type of event (EV_<type> value)
 * @param flags Flags for the given event type (<type>_EV_ bitmask)
 * @param value The event payload (use the <type>_VALUE function)
 * @return
 */
int input_event(input_evtyp_t type, uint16_t flags, uint32_t value);

/**
 * Returns the current state of the given key.
 * @param key The key code
 * @return
 */
int input_getkey(uint16_t key);

int input_key_event_to_char(input_key_event_t *event);

#endif
