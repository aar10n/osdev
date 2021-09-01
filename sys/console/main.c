//
// Created by Aaron Gill-Braun on 2021-08-17.
//

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <osdev/event.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define FB_WIDTH 1024
#define FB_HEIGHT 768
#define FB_PPS 1024
#define FB_SIZE (FB_PPS * FB_HEIGHT * sizeof(uint32_t))
#define fb_index(i, j) ((y * 8) + (i)) * FB_PPS + ((x * 8) + (j))

uint32_t *fb;
FT_Library library;
FT_Face face;

const char *font = "/usr/share/fonts/truetype/routed-gothic.ttf";
const char *text = "Hello, world!";


int main(int argc, const char **argv) {
  int events = open("/dev/events", O_RDONLY);
  if (events < 0) {
    fprintf(stderr, "failed to open /dev/events: %s\n", strerror(errno));
    exit(1);
  }

  int framebuf = open("/dev/fb0", O_WRONLY);
  if (framebuf < 0) {
    fprintf(stderr, "failed to open /dev/fb0: %s\n", strerror(errno));
  }

  struct stat st;
  int result = fstat(framebuf, &st);
  if (result < 0) {
    fprintf(stderr, "failed to stat framebuffer\n");
    exit(1);
  }

  fb = mmap(NULL, FB_SIZE, PROT_WRITE, 0, framebuf, 0);
  if (fb == MAP_FAILED) {
    fprintf(stderr, "failed to mmap framebuffer\n");
    exit(1);
  }
  memset(fb, UINT32_MAX, FB_SIZE);

  //

  FT_Error error;
  error = FT_Init_FreeType(&library);
  if (error) {
    fprintf(stderr, "failed to initialize freetype: %s\n", FT_Error_String(error));
    exit(1);
  }

  error = FT_New_Face(library, font, 0, &face);
  if (error == FT_Err_Unknown_File_Format) {
    fprintf(stderr, "unsupported font format: %s\n", font);
    exit(1);
  } else if (error) {
    fprintf(stderr, "failed to load font: %s\n", FT_Error_String(error));
    exit(1);
  }

  error = FT_Set_Pixel_Sizes (face, 0, 20);
  if (error) {
    fprintf(stderr, "failed to set char size: %s\n", FT_Error_String(error));
    exit(1);
  }

  printf("font successfully loaded!!!\n");
  printf("num_glyphs: %ld\n", face->num_glyphs);

  int x = 300;
  int y = 200;
  size_t len = strlen(text);
  for (int c = 0; c < len; c++) {
    error = FT_Load_Char(face, text[c], FT_LOAD_RENDER);
    if (error) {
      fprintf(stderr, "failed to load char %c: %s\n", text[c], FT_Error_String(error));
      exit(1);
    }

    FT_Bitmap *bmp = &face->glyph->bitmap;

    int bbox_ymax = face->bbox.yMax / 64;
    int glyph_width = face->glyph->metrics.width / 64;
    int advance = face->glyph->metrics.horiAdvance / 64;
    int x_off = (advance - glyph_width) / 2;
    int y_off = bbox_ymax - face->glyph->metrics.horiBearingY / 64;

    for (int i = 0; i < bmp->rows; i++) {
      int row_off = y + i + y_off;
      for (int j = 0; j < bmp->width; j++) {
        int col_off = x + j + x_off;

        uint8_t p = bmp->buffer[i * bmp->pitch + j];
        if (p) {
          uint8_t cl = ~(uint8_t)(0xFF * ((float) p / 255));
          uint32_t v = cl | (cl << 8) | (cl << 16);
          int index = row_off * FB_PPS + col_off;
          fb[index] = v;
        }

      }
    }

    x += face->glyph->advance.x >> 6;
  }



  // key_event_t event;
  // while ((read(events, &event, sizeof(key_event_t))) > 0) {
  //   printf("key code: %d | release: %d (%x)\n", event.key_code, event.release, event.modifiers);
  //   if (event.key_code == VK_KEYCODE_ESCAPE) {
  //     break;
  //   }
  // }

  return 0;
}
