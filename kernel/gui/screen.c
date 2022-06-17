//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#include <gui/screen.h>
#include <gui/font8x8_basic.h>
#include <string.h>

#define WIDTH boot_info_v2->fb_width
#define HEIGHT boot_info_v2->fb_height
#define index(i, j) ((y * 8) + (i)) * WIDTH + ((x * 8) + (j))

static int x = 0;
static int y = 0;
static uint32_t *fb = (void *) FRAMEBUFFER_VA;

void screen_print_char(char ch) {
  switch (ch) {
    case '\n':
      x = 0;
      y += 1;
      return;
    case '\r':
      x = 0;
      return;
    case '\f':
      y += 1;
      return;
    case '\t':
      x += 4;
      return;
    case '\b':
      x = max(x - 1, 0);
      for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
          fb[index(i, j)] = 0;
        }
      }
      return;
    default:
      break;
  }

  char *letter = font8x8_basic[(uint8_t) ch];
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      if (letter[i] & (1 << j)) {
        fb[index(i, j)] = 0xFFFFFFFF;
      }
    }
  }
  x++;
}

void screen_print_str(const char *string) {
  int len = strlen(string);
  for (int i = 0; i < len; i++) {
    screen_print_char(string[i]);
  }
}
