//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/cpu/asm.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>

#include <kernel/cpu/cpu.h>
#include <kernel/cpu/interrupt.h>
#include <kernel/mem/heap.h>
#include <string.h>

char *translate_scancode(char scancode);

static void keyboard_callback(registers_t regs) {
  /* The PIC leaves us the scancode in port 0x60 */
  uint8_t scancode = inb(0x60);
  char *key = translate_scancode(scancode);
  kprintf("keypress | %#X (%#b) - %s\n", scancode, scancode, key);
  int is_keyup = (1 << 7) & scancode;
  if (is_keyup) {
    kfree(key);
  }
}

char *translate_scancode(char scancode) {
  int is_keyup = (1 << 7) & scancode;
  if (is_keyup) {
    char keyup_char = ~(1 << 7) & scancode;
    char* key = translate_scancode(keyup_char);
    size_t len = strlen(key);
    char *string = kmalloc(len + 4);
    strcpy(string, key);
    strcpy(string + len, " up");
    return string;
  }

  switch (scancode) {
    case 0x1:
      return "Esc";
    case 0x2:
      return "1";
    case 0x3:
      return "2";
    case 0x4:
      return "3";
    case 0x5:
      return "4";
    case 0x6:
      return "5";
    case 0x7:
      return "6";
    case 0x8:
      return "7";
    case 0x9:
      return "8";
    case 0x0A:
      return "9";
    case 0x0B:
      return "0";
    case 0x0C:
      return "-";
    case 0x0D:
      return "+";
    case 0x0E:
      return "Backspace";
    case 0x0F:
      return "Tab";
    case 0x10:
      return "q";
    case 0x11:
      return "w";
    case 0x12:
      return "e";
    case 0x13:
      return "r";
    case 0x14:
      return "t";
    case 0x15:
      return "y";
    case 0x16:
      return "u";
    case 0x17:
      return "i";
    case 0x18:
      return "o";
    case 0x19:
      return "p";
    case 0x1A:
      return "[";
    case 0x1B:
      return "]";
    case 0x1C:
      return "Enter";
    case 0x1D:
      return "LCtrl";
    case 0x1E:
      return "a";
    case 0x1F:
      return "s";
    case 0x20:
      return "d";
    case 0x21:
      return "f";
    case 0x22:
      return "g";
    case 0x23:
      return "h";
    case 0x24:
      return "j";
    case 0x25:
      return "k";
    case 0x26:
      return "l";
    case 0x27:
      return ";";
    case 0x28:
      return "'";
    case 0x29:
      return "`";
    case 0x2A:
      return "LShift";
    case 0x2B:
      return "\\";
    case 0x2C:
      return "z";
    case 0x2D:
      return "x";
    case 0x2E:
      return "c";
    case 0x2F:
      return "v";
    case 0x30:
      return "b";
    case 0x31:
      return "n";
    case 0x32:
      return "m";
    case 0x33:
      return ",";
    case 0x34:
      return ".";
    case 0x35:
      return "/";
    case 0x36:
      return "RShift";
    case 0x37:
      return "Keypad  *";
    case 0x38:
      return "LAlt";
    case 0x39:
      return "Space";
    case 0x3A:
      return "CapsLock";
    case 0x3B:
      return "F1";
    case 0x3C:
      return "F2";
    case 0x3D:
      return "F3";
    case 0x3E:
      return "F4";
    case 0x3F:
      return "F5";
    case 0x40:
      return "F6";
    case 0x41:
      return "F7";
    case 0x42:
      return "F8";
    case 0x43:
      return "F9";
    case 0x44:
      return "F10";
    case 0x45:
      return "NumLock";
    case 0x46:
      return "ScrollLock";
    case 0x47:
      return "Keypad 7";
    case 0x48:
      return "Keypad 8";
    case 0x49:
      return "Keypad 9";
    case 0x4A:
      return "Keypad -";
    case 0x4B:
      return "Keypad 4";
    case 0x4C:
      return "Keypad 5";
    case 0x4D:
      return "Keypad 6";
    case 0x4E:
      return "Keypad +";
    case 0x4F:
      return "Keypad 1";
    case 0x50:
      return "Keypad 2";
    case 0x51:
      return "Keypad 3";
    case 0x52:
      return "Keypad 0";
    case 0x53:
      return "Keypad Del";
    case 0x54:
      return "?Alt?";
    case 0x5b:
      return "Command";
    default:
      return "";
  }
}

void init_keyboard() {
  register_isr(IRQ1, keyboard_callback);
}
