//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#include <kernel/gui/screen.h>
#include <kernel/gui/font8x8_basic.h>

#include <kernel/mm.h>
#include <kernel/init.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#define WIDTH boot_info_v2->fb_width
#define HEIGHT boot_info_v2->fb_height

static int x = 0;
static int y = 0;
__used uint32_t *framebuf_base;


void remap_framebuffer_mmio(void *data) {
  framebuf_base = (void *) vm_alloc_map_phys(boot_info_v2->fb_addr, FRAMEBUFFER_VA, boot_info_v2->fb_size,
                                             VM_FIXED, PG_WRITE, "framebuffer")->address;
}

//

void screen_early_init() {
  kprintf("framebuffer:\n");
  kprintf("  width: %zu\n", boot_info_v2->fb_width);
  kprintf("  height: %zu\n", boot_info_v2->fb_height);
  kprintf("  size: %zu\n", boot_info_v2->fb_size);
  framebuf_base = (void *) boot_info_v2->fb_addr;
  register_init_address_space_callback(remap_framebuffer_mmio, NULL);
}

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
          size_t index = ((y * 8) + (i)) * WIDTH + ((x * 8) + (j));
          framebuf_base[index] = 0;
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
        size_t index = ((y * 8) + (i)) * WIDTH + ((x * 8) + (j));
        framebuf_base[index] = UINT32_MAX;
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

//

static void screen_init() {
  // clear screen
  __memset8((void *) FRAMEBUFFER_VA, 0x00, boot_info_v2->fb_size);
}
STATIC_INIT(screen_init);
