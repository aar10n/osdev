//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#ifndef KERNEL_USB_HID_USAGE_H
#define KERNEL_USB_HID_USAGE_H

#include <kernel/base.h>

// usage pages
#define GENERIC_DESKTOP_PAGE 0x01
#define   POINTER_USAGE    0x01
#define   MOUSE_USAGE      0x02
#define   JOYSTICK_USAGE   0x04
#define   GAMEPAD_USAGE    0x05
#define   KEYBOARD_USAGE   0x06
#define   KEYPAD_USAGE     0x07
// ...
#define   X_USAGE          0x30
#define   Y_USAGE          0x31
#define   Z_USAGE          0x32
#define   RX_USAGE         0x33
#define   RY_USAGE         0x34
#define   RZ_USAGE         0x35
#define   SLIDER_USAGE     0x36
#define   DIAL_USAGE       0x37
#define   WHEEL_USAGE      0x38
#define   HAT_SWITCH_USAGE 0x39
// ...

#define SIMULATION_CONTROLS_PAGE 0x02

#define VR_CONTROLS_PAGE 0x03

#define SPORT_CONTROLS_PAGE 0x04

#define GAME_CONTROLS_PAGE 0x05

#define GENERIC_DEVICE_CONTROLS_PAGE 0x06

#define KEYBOARD_PAGE 0x07

#define LED_PAGE 0x08

#define BUTTON_PAGE 0x09


const char *hid_get_usage_page_name(uint8_t page);
const char *hid_get_usage_name(uint8_t page, uint8_t usage);

#endif
