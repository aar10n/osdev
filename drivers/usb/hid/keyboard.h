//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#ifndef KERNEL_USB_KEYBOARD_H
#define KERNEL_USB_KEYBOARD_H

#include <kernel/base.h>

#include "hid.h"

// HID Keyboard Driver

#define HID_KEYBOARD_A            0x04   // a and A
#define HID_KEYBOARD_B            0x05   // b and B
#define HID_KEYBOARD_C            0x06   // c and C
#define HID_KEYBOARD_D            0x07   // d and D
#define HID_KEYBOARD_E            0x08   // e and E
#define HID_KEYBOARD_F            0x09   // f and F
#define HID_KEYBOARD_G            0x0A   // g and G
#define HID_KEYBOARD_H            0x0B   // h and H
#define HID_KEYBOARD_I            0x0C   // i and I
#define HID_KEYBOARD_J            0x0D   // j and J
#define HID_KEYBOARD_K            0x0E   // k and K
#define HID_KEYBOARD_L            0x0F   // l and L
#define HID_KEYBOARD_M            0x10   // m and M
#define HID_KEYBOARD_N            0x11   // n and N
#define HID_KEYBOARD_O            0x12   // o and O
#define HID_KEYBOARD_P            0x13   // p and P
#define HID_KEYBOARD_Q            0x14   // q and Q
#define HID_KEYBOARD_R            0x15   // r and R
#define HID_KEYBOARD_S            0x16   // s and S
#define HID_KEYBOARD_T            0x17   // t and T
#define HID_KEYBOARD_U            0x18   // u and U
#define HID_KEYBOARD_V            0x19   // v and V
#define HID_KEYBOARD_W            0x1A   // w and W
#define HID_KEYBOARD_X            0x1B   // x and X
#define HID_KEYBOARD_Y            0x1C   // y and Y
#define HID_KEYBOARD_Z            0x1D   // z and Z
#define HID_KEYBOARD_1            0x1E   // 1 and !
#define HID_KEYBOARD_2            0x1F   // 2 and @
#define HID_KEYBOARD_3            0x20   // 3 and #
#define HID_KEYBOARD_4            0x21   // 4 and $
#define HID_KEYBOARD_5            0x22   // 5 and %
#define HID_KEYBOARD_6            0x23   // 6 and ^
#define HID_KEYBOARD_7            0x24   // 7 and &
#define HID_KEYBOARD_8            0x25   // 8 and *
#define HID_KEYBOARD_9            0x26   // 9 and (
#define HID_KEYBOARD_0            0x27   // 0 and )
#define HID_KEYBOARD_RETURN       0x28   // return (enter)
#define HID_KEYBOARD_ESCAPE       0x29   // escape
#define HID_KEYBOARD_DELETE       0x2A   // delete (backspace)
#define HID_KEYBOARD_TAB          0x2B   // tab
#define HID_KEYBOARD_SPACE        0x2C   // spacebar
#define HID_KEYBOARD_MINUS        0x2D   // - and _
#define HID_KEYBOARD_EQUAL        0x2E   // = and +
#define HID_KEYBOARD_LSQUARE      0x2F   // [ and {
#define HID_KEYBOARD_RSQUARE      0x30   // ] and }
#define HID_KEYBOARD_BACKSLASH    0x31   // \ and |
#define HID_KEYBOARD_SEMICOLON    0x33   // ; and :
#define HID_KEYBOARD_APOSTROPHE   0x34   // ' and "
#define HID_KEYBOARD_TILDE        0x35   // ` and ~
#define HID_KEYBOARD_COMMA        0x36   //  and <
#define HID_KEYBOARD_PERIOD       0x37   // . and >
#define HID_KEYBOARD_SLASH        0x38   // / and ?
#define HID_KEYBOARD_CAPSLOCK     0x39   // caps lock
#define HID_KEYBOARD_F1           0x3A   // F1
#define HID_KEYBOARD_F2           0x3B   // F2
#define HID_KEYBOARD_F3           0x3C   // F3
#define HID_KEYBOARD_F4           0x3D   // F4
#define HID_KEYBOARD_F5           0x3E   // F5
#define HID_KEYBOARD_F6           0x3F   // F6
#define HID_KEYBOARD_F7           0x40   // F7
#define HID_KEYBOARD_F8           0x41   // F8
#define HID_KEYBOARD_F9           0x42   // F9
#define HID_KEYBOARD_F10          0x43   // F10
#define HID_KEYBOARD_F11          0x44   // F11
#define HID_KEYBOARD_F12          0x45   // F12
#define HID_KEYBOARD_PRINTSCR     0x46   // print screen
#define HID_KEYBOARD_SCROLL_LOCK  0x47   // scroll lock
#define HID_KEYBOARD_PAUSE        0x48   // pause
#define HID_KEYBOARD_INSERT       0x49   // insert
#define HID_KEYBOARD_HOME         0x4A   // home
#define HID_KEYBOARD_PAGE_UP      0x4B   // page up
#define HID_KEYBOARD_DELETE_FWD   0x4C   // delete forward
#define HID_KEYBOARD_END          0x4D   // end
#define HID_KEYBOARD_PAGE_DOWN    0x4E   // page down
#define HID_KEYBOARD_RIGHT        0x4F   // right arrow
#define HID_KEYBOARD_LEFT         0x50   // left arrow
#define HID_KEYBOARD_DOWN         0x51   // down arrow
#define HID_KEYBOARD_UP           0x52   // up arrow
#define HID_KEYBOARD_NUM_LOCK     0x53   // num lock

// keypad keys
#define HID_KEYBOARD_KP_SLASH     0x54   // keypad /
#define HID_KEYBOARD_KP_ASTERISK  0x55   // keypad *
#define HID_KEYBOARD_KP_MINUS     0x56   // keypad -
#define HID_KEYBOARD_KP_PLUS      0x57   // keypad +
#define HID_KEYBOARD_KP_ENTER     0x58   // keypad enter
#define HID_KEYBOARD_KP_1         0x59   // keypad 1
#define HID_KEYBOARD_KP_2         0x5A   // keypad 2
#define HID_KEYBOARD_KP_3         0x5B   // keypad 3
#define HID_KEYBOARD_KP_4         0x5C   // keypad 4
#define HID_KEYBOARD_KP_5         0x5D   // keypad 5
#define HID_KEYBOARD_KP_6         0x5E   // keypad 6
#define HID_KEYBOARD_KP_7         0x5F   // keypad 7
#define HID_KEYBOARD_KP_8         0x60   // keypad 8
#define HID_KEYBOARD_KP_9         0x61   // keypad 9
#define HID_KEYBOARD_KP_0         0x62   // keypad 0
#define HID_KEYBOARD_KP_PERIOD    0x63   // keypad .

// international keys
#define HID_KEYBOARD_NON_US_BACKSLASH 0x64  // non-US \ and |
#define HID_KEYBOARD_APPLICATION  0x65   // application key
#define HID_KEYBOARD_POWER        0x66   // power
#define HID_KEYBOARD_KP_EQUAL     0x67   // keypad =

// function keys F13-F24
#define HID_KEYBOARD_F13          0x68   // F13
#define HID_KEYBOARD_F14          0x69   // F14
#define HID_KEYBOARD_F15          0x6A   // F15
#define HID_KEYBOARD_F16          0x6B   // F16
#define HID_KEYBOARD_F17          0x6C   // F17
#define HID_KEYBOARD_F18          0x6D   // F18
#define HID_KEYBOARD_F19          0x6E   // F19
#define HID_KEYBOARD_F20          0x6F   // F20
#define HID_KEYBOARD_F21          0x70   // F21
#define HID_KEYBOARD_F22          0x71   // F22
#define HID_KEYBOARD_F23          0x72   // F23
#define HID_KEYBOARD_F24          0x73   // F24

// system keys
#define HID_KEYBOARD_EXECUTE      0x74   // execute
#define HID_KEYBOARD_HELP         0x75   // help
#define HID_KEYBOARD_MENU         0x76   // menu
#define HID_KEYBOARD_SELECT       0x77   // select
#define HID_KEYBOARD_STOP         0x78   // stop
#define HID_KEYBOARD_AGAIN        0x79   // again
#define HID_KEYBOARD_UNDO         0x7A   // undo
#define HID_KEYBOARD_CUT          0x7B   // cut
#define HID_KEYBOARD_COPY         0x7C   // copy
#define HID_KEYBOARD_PASTE        0x7D   // paste
#define HID_KEYBOARD_FIND         0x7E   // find
#define HID_KEYBOARD_MUTE         0x7F   // mute
#define HID_KEYBOARD_VOLUME_UP    0x80   // volume up
#define HID_KEYBOARD_VOLUME_DOWN  0x81   // volume down

#define HID_KEYBOARD_LCONTROL     0xE0 // left control
#define HID_KEYBOARD_LSHIFT       0xE1 // left shift
#define HID_KEYBOARD_LALT         0xE2 // left alt
#define HID_KEYBOARD_LGUI         0xE3 // left command/windows key
#define HID_KEYBOARD_RCONTROL     0xE4 // right control
#define HID_KEYBOARD_RSHIFT       0xE5 // right shift
#define HID_KEYBOARD_RALT         0xE6 // right alt
#define HID_KEYBOARD_RGUI         0xE7 // right command/windows key

#define HID_BIT_LCONTROL 0
#define HID_BIT_LSHIFT   1
#define HID_BIT_LALT     2
#define HID_BIT_LSPECIAL 3
#define HID_BIT_RCONTROL 4
#define HID_BIT_RSHIFT   5
#define HID_BIT_RALT     6
#define HID_BIT_RSPECIAL 7

typedef struct {
  uint8_t modifier_offset;
  uint8_t led_offset;
  uint8_t buffer_offset;
  uint8_t buffer_size;

  uint8_t *prev_buffer;
} hid_keyboard_t;


hid_keyboard_t *hid_keyboard_init(report_format_t *format);
void hid_keyboard_handle_input(hid_device_t *hid_dev, uint8_t *buffer);

#endif
