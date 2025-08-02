//
// Created by Aaron Gill-Braun on 2025-07-27.
//

#ifndef INCLUDE_UAPI_INPUT_EVENT_CODES_H
#define INCLUDE_UAPI_INPUT_EVENT_CODES_H

#include <stdint.h>

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h

// event types
#define EV_KEY			    0x01 // key events
#define EV_REL		      0x02 // relative mouse events
#define EV_ABS		      0x03 // absolute mouse events
#define   EV_MAX			    0x1f
#define   EV_CNT			    (EV_MAX+1)

// MARK: Key Codes

// modifiers
#define KEY_LCTRL       0x001 // left control
#define KEY_LSHIFT      0x002 // left shift
#define KEY_LALT        0x003 // left alt
#define KEY_LMETA       0x004 // left special/command
#define KEY_RCTRL       0x005 // right control
#define KEY_RSHIFT      0x006 // right shift
#define KEY_RALT        0x007 // right alt
#define KEY_RMETA       0x008 // right special/command
// letters
#define KEY_A           0x009 // a and A
#define KEY_B           0x00A // b and B
#define KEY_C           0x00B // c and C
#define KEY_D           0x00C // d and D
#define KEY_E           0x00D // e and E
#define KEY_F           0x00E // f and F
#define KEY_G           0x00F // g and G
#define KEY_H           0x010 // h and H
#define KEY_I           0x011 // i and I
#define KEY_J           0x012 // j and J
#define KEY_K           0x013 // k and K
#define KEY_L           0x014 // l and L
#define KEY_M           0x015 // m and M
#define KEY_N           0x016 // n and N
#define KEY_O           0x017 // o and O
#define KEY_P           0x018 // p and P
#define KEY_Q           0x019 // q and Q
#define KEY_R           0x01A // r and R
#define KEY_S           0x01B // s and S
#define KEY_T           0x01C // t and T
#define KEY_U           0x01D // u and U
#define KEY_V           0x01E // v and V
#define KEY_W           0x01F // w and W
#define KEY_X           0x020 // x and X
#define KEY_Y           0x021 // y and Y
#define KEY_Z           0x022 // z and Z
// numbers
#define KEY_1           0x023 // 1 and !
#define KEY_2           0x024 // 2 and @
#define KEY_3           0x025 // 3 and #
#define KEY_4           0x026 // 4 and $
#define KEY_5           0x027 // 5 and %
#define KEY_6           0x028 // 6 and ^
#define KEY_7           0x029 // 7 and &
#define KEY_8           0x02A // 8 and *
#define KEY_9           0x02B // 9 and (
#define KEY_0           0x02C // 0 and )
// function keys
#define KEY_F1          0x02D // F1
#define KEY_F2          0x02E // F2
#define KEY_F3          0x02F // F3
#define KEY_F4          0x030 // F4
#define KEY_F5          0x031 // F5
#define KEY_F6          0x032 // F6
#define KEY_F7          0x033 // F7
#define KEY_F8          0x034 // F8
#define KEY_F9          0x035 // F9
#define KEY_F10         0x036 // F10
#define KEY_F11         0x037 // F11
#define KEY_F12         0x038 // F12
#define KEY_F13         0x058 // F13
#define KEY_F14         0x059 // F14
#define KEY_F15         0x05A // F15
#define KEY_F16         0x05B // F16
#define KEY_F17         0x05C // F17
#define KEY_F18         0x05D // F18
#define KEY_F19         0x05E // F19
#define KEY_F20         0x05F // F20
#define KEY_F21         0x060 // F21
#define KEY_F22         0x061 // F22
#define KEY_F23         0x062 // F23
#define KEY_F24         0x063 // F24
// other
#define KEY_ENTER       0x039 // return (enter)
#define KEY_ESCAPE      0x03A // escape
#define KEY_BACKSPACE   0x03B // delete (backspace)
#define KEY_TAB         0x03C // tab
#define KEY_SPACE       0x03D // spacebar
#define KEY_CAPSLOCK    0x03E // caps lock
// special
#define KEY_MINUS       0x03F // - and _
#define KEY_EQUAL       0x040 // = and +
#define KEY_LSQUARE     0x041 // [ and {
#define KEY_RSQUARE     0x042 // ] and }
#define KEY_BACKSLASH   0x043 // \ and |
#define KEY_SEMICOLON   0x044 // ; and :
#define KEY_APOSTROPHE  0x045 // ' and "
#define KEY_GRAVE       0x046 // ` and ~
#define KEY_COMMA       0x047 // , and <
#define KEY_PERIOD      0x048 // . and >
#define KEY_SLASH       0x049 // / and ?
// arrow keys
#define KEY_RIGHT       0x04A // right arrow
#define KEY_LEFT        0x04B // left arrow
#define KEY_DOWN        0x04C // down arrow
#define KEY_UP          0x04D // up arrow
// media keys
#define KEY_PRINTSCR    0x04E // print screen
#define KEY_SCROLL_LOCK 0x04F // scroll lock
#define KEY_NUM_LOCK    0x050 // num lock
#define KEY_PAUSE       0x051 // pause
#define KEY_INSERT      0x052 // insert
#define KEY_HOME        0x053 // home
#define KEY_END         0x054 // end
#define KEY_PAGE_UP     0x055 // page up
#define KEY_PAGE_DOWN   0x056 // page down
#define KEY_DELETE      0x057 // delete forward

// keypad keys
#define KEY_KP_SLASH    0x064 // keypad /
#define KEY_KP_ASTERISK 0x065 // keypad *
#define KEY_KP_MINUS    0x066 // keypad -
#define KEY_KP_PLUS     0x067 // keypad +
#define KEY_KP_ENTER    0x068 // keypad enter
#define KEY_KP_1        0x069 // keypad 1
#define KEY_KP_2        0x06A // keypad 2
#define KEY_KP_3        0x06B // keypad 3
#define KEY_KP_4        0x06C // keypad 4
#define KEY_KP_5        0x06D // keypad 5
#define KEY_KP_6        0x06E // keypad 6
#define KEY_KP_7        0x06F // keypad 7
#define KEY_KP_8        0x070 // keypad 8
#define KEY_KP_9        0x071 // keypad 9
#define KEY_KP_0        0x072 // keypad 0
#define KEY_KP_PERIOD   0x073 // keypad .
#define KEY_KP_EQUAL    0x074 // keypad =

// system and special keys
#define KEY_APPLICATION 0x075 // application key
#define KEY_POWER       0x076 // power
#define KEY_EXECUTE     0x077 // execute
#define KEY_HELP        0x078 // help
#define KEY_MENU        0x079 // menu
#define KEY_SELECT      0x07A // select
#define KEY_STOP        0x07B // stop
#define KEY_AGAIN       0x07C // again
#define KEY_UNDO        0x07D // undo
#define KEY_CUT         0x07E // cut
#define KEY_COPY        0x07F // copy
#define KEY_PASTE       0x080 // paste
#define KEY_FIND        0x081 // find
#define KEY_MUTE        0x082 // mute
#define KEY_VOLUME_UP   0x083 // volume up
#define KEY_VOLUME_DOWN 0x084 // volume down

// MARK: Mouse Buttons

// mouse buttons
#define BTN_MOUSE1      0x085 // left mouse button
#define BTN_MOUSE2      0x086 // right mouse button
#define BTN_MOUSE3      0x087 // middle mouse button

#define KEY_MAX			    0x90
#define KEY_CNT			    (KEY_MAX+1)

#endif
