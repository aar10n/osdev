//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#ifndef KERNEL_USB_KEYBOARD_H
#define KERNEL_USB_KEYBOARD_H

#include <base.h>
#include <usb/hid.h>
#include <usb/hid-report.h>

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

#define HID_KEYBOARD_LCONTROL     0xE0 // left control
#define HID_KEYBOARD_LSHIFT       0xE1 // left shift
#define HID_KEYBOARD_LALT         0xE2 // left alt
#define HID_KEYBOARD_LGUI         0xE3 // left command/windows key
#define HID_KEYBOARD_RCONTROL     0xE4 // right control
#define HID_KEYBOARD_RSHIFT       0xE5 // right shift
#define HID_KEYBOARD_RALT         0xE6 // right alt
#define HID_KEYBOARD_RGUI         0xE7 // right command/windows key

typedef struct {
  uint8_t modifier_offset;
  uint8_t led_offset;
  uint8_t buffer_offset;
  uint8_t buffer_size;

  uint8_t *prev_buffer;
} hid_keyboard_t;


hid_keyboard_t *hid_keyboard_init(report_format_t *format);
void hid_keyboard_handle_input(hid_device_t *device, const uint8_t *buffer);

#endif
