//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include "hid-usage.h"

// hid usage tables

const char *usage_page_names[] = {
  [GENERIC_DESKTOP_PAGE] = "Generic Desktop",
  [SIMULATION_CONTROLS_PAGE] = "Simulation Controls",
  [VR_CONTROLS_PAGE] = "VR Controls",
  [SPORT_CONTROLS_PAGE] = "Sport Controls",
  [GAME_CONTROLS_PAGE] = "Game Controls",
  [GENERIC_DEVICE_CONTROLS_PAGE] = "Generic Device Controls",
  [KEYBOARD_PAGE] = "Keyboard/Keypad",
  [LED_PAGE] = "LED",
  [BUTTON_PAGE] = "Button"
};

const char *generic_desktop_usage_names[] = {
  [POINTER_USAGE] = "Pointer",
  [MOUSE_USAGE] = "Mouse",
  [JOYSTICK_USAGE] = "Joystick",
  [GAMEPAD_USAGE] = "Gamepad",
  [KEYBOARD_USAGE] = "Keyboard",
  [KEYPAD_USAGE] = "Keypad",
  // ...
  [X_USAGE] = "X",
  [Y_USAGE] = "Y",
  [Z_USAGE] = "Z",
  [RX_USAGE] = "Rx",
  [RY_USAGE] = "Ry",
  [RZ_USAGE] = "Rz",
  [SLIDER_USAGE] = "Slider",
  [DIAL_USAGE] = "Dial",
  [WHEEL_USAGE] = "Wheel",
  [HAT_SWITCH_USAGE] = "Hat Switch",
  // ...
};

const char *hid_get_usage_page_name(uint8_t page) {
  size_t len = sizeof(usage_page_names) / sizeof(char *);
  if (page >= len) {
    return NULL;
  }
  return usage_page_names[page];
}

const char *hid_get_usage_name(uint8_t page, uint8_t usage) {
  size_t len;
  const char **array;
  if (page == GENERIC_DESKTOP_PAGE) {
    len = sizeof(generic_desktop_usage_names) / sizeof(char *);
    array = generic_desktop_usage_names;
  } else {
    return NULL;
  }

  if (usage >= len) {
    return NULL;
  }
  return array[usage];
}
