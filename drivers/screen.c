//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/cpu/asm.h>
#include <drivers/screen.h>

typedef struct {
  int x;
  int y;
  int pos;
} cursor_t;

static cursor_t cursor = { 0, 0, 0 };


void set() {
  cursor.pos = (cursor.y * MAX_COLS + cursor.x) * 2;
  outb(VGA_CTRL_PORT, 14);
  outb(VGA_DATA_PORT, ((cursor.pos / 2) >> 8));
  outb(VGA_CTRL_PORT, 15);
  outb(VGA_DATA_PORT, ((cursor.pos / 2) & 0xFF));
}

void scroll() {
  for (int i = 0; i < MAX_ROWS; i++) {
    memcpy(VIDEO_ADDRESS + ((i - 1) * MAX_COLS * 2), VIDEO_ADDRESS + (i * MAX_COLS * 2), MAX_COLS * 2);
  }

  memset(VIDEO_ADDRESS + ((MAX_ROWS - 1) * MAX_COLS * 2), 0x0, MAX_COLS * 2);

  cursor.x = 0;
  cursor.y = MAX_ROWS - 1;
  set();
}

void update() {
  if (cursor.x >= MAX_COLS - 1) {
    cursor.x = 0;
    cursor.y++;
    set();
    return;
  } else if (cursor.y >= MAX_ROWS) {
    cursor.y = MAX_ROWS;
    scroll();
    return;
  } else {
    set();
    return;
  }
}

//
//
// Public API Functions
//
//

void kputc(char c) {
  switch (c) {
    case '\n':
      cursor.x = 0;
      cursor.y++;
      update();
      return;
    case '\r':
      cursor.x = 0;
      update();
      return;
    case '\f':
      cursor.y++;
      update();
      return;
    default:
      break;
  }

  char *vga = VIDEO_ADDRESS;
  vga[cursor.pos] = c;
  vga[cursor.pos + 1] = 0x07;

  cursor.x++;
  update();
}

void kputs(char *s) {
  while (*s != 0) {
    kputc(*s);
    s++;
  }
}

void kclear() {
  memset(VIDEO_ADDRESS, 0x00, MAX_ROWS * MAX_COLS * 2);
  cursor.x = 0;
  cursor.y = 0;
  update();
}
