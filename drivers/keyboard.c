//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <drivers/asm.h>

#include <kernel/cpu/isr.h>

#include <stdint.h>
#include "../libc/stdlib.h"
#include <stddef.h>
#include "../libc/stdio.h"

char *translate_scancode(char scancode);

static void keyboard_callback(registers_t regs) {
  /* The PIC leaves us the scancode in port 0x60 */
  uint8_t scancode = inb(0x60);
  char *key = translate_scancode(scancode);
  kprintf("keypress | %d -- '%s'\n", scancode, key);
}

char *translate_scancode(char scancode) {
  switch (scancode) {
    case 0x2:  return "1";
    case 0x3:  return "2";
    case 0x4:  return "3";
    case 0x5:  return "4";
    case 0x6:  return "5";
    case 0x7:  return "6";
    case 0x8:  return "7";
    case 0x9:  return "8";
    case 0x0A: return "9";
    case 0x0B: return "0";
    case 0x0C: return "-";
    case 0x0D: return "+";
    case 0x0E: return "Backspace";
    case 0x0F: return "Tab";
    case 0x10: return "q";
    case 0x11: return "w";
    case 0x12: return "e";
    case 0x13: return "r";
    case 0x14: return "t";
    case 0x15: return "y";
    case 0x16: return "u";
    case 0x17: return "i";
    case 0x18: return "o";
    case 0x19: return "p";
    case 0x1A: return "[";
    case 0x1B: return "]";
    case 0x1C: return "Enter";
    case 0x1D: return "LCtrl";
    case 0x1E: return "a";
    case 0x1F: return "s";
    case 0x20: return "d";
    case 0x21: return "f";
    case 0x22: return "g";
    case 0x23: return "h";
    case 0x24: return "j";
    case 0x25: return "k";
    case 0x26: return "l";
    case 0x27: return ";";
    case 0x28: return "'";
    case 0x29: return "`";
    case 0x2A: return "LShift";
    case 0x2B: return "\\";
    case 0x2C: return "z";
    case 0x2D: return "x";
    case 0x2E: return "c";
    case 0x2F: return "v";
    case 0x30: return "b";
    case 0x31: return "n";
    case 0x32: return "m";
    case 0x33: return ",";
    case 0x34: return ".";
    case 0x35: return "/";
    case 0x36: return "RShift";
    case 0x37: return "Keypad  *";
    case 0x38: return "LAlt";
    case 0x39: return "Space";
    case 0x3A: return "v";
    case 0x3B: return "v";
    case 0x3C: return "v";
    case 0x3D: return "v";
    case 0x3E: return "v";
    case 0x3F: return "v";
    default: return "";
  }
}

void init_keyboard() {
  register_interrupt_handler(IRQ1, keyboard_callback);
}
