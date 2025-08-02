//
// Created by Aaron Gill-Braun on 2025-07-27.
//

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <m_argv.h>
#include <doomgeneric.h>
#include <doomkeys.h>

#include <osdev/framebuf.h>
#include <osdev/input.h>
#include <osdev/input-event-codes.h>

#define _used __attribute__((used))

static int fb_fd = -1;
static struct fb_info fbinfo;
static char *fb_ptr = NULL;
static size_t fb_width = 0;
static size_t fb_height = 0;
static size_t fb_bytesperpixel = 0;
static size_t fb_stride = 0;
static size_t fb_xoffset = 0;
static size_t fb_yoffset = 0;

// arguments
static uint32_t target_fps = 0;
static uint32_t target_frame_time;
static int scale_to_fullscreen = 0;


// key queue
#define KEY_QUEUE_SIZE 16
static struct {
  unsigned char key;
  int pressed;
} key_queue[KEY_QUEUE_SIZE];
static int key_read_idx = 0;
static int key_write_idx = 0;

// last key states for detecting key releases
static int key_states[KEY_MAX] = {0};
int kbd_fd;

pixel_t* DG_ScreenBuffer = NULL;
void D_DoomMain();

static unsigned char keycode_to_doom(int code) {
  switch(code) {
    case KEY_LCTRL:
    case KEY_RCTRL:
      return DOOM_KEY_FIRE;
    case KEY_LSHIFT:
    case KEY_RSHIFT:
      return DOOM_KEY_USE;
    case KEY_LALT:
      return DOOM_KEY_STRAFE_L;
    case KEY_RALT:
      return DOOM_KEY_STRAFE_R;

    case KEY_W:
      return DOOM_KEY_UPARROW;
    case KEY_A:
      return DOOM_KEY_LEFTARROW;
    case KEY_S:
      return DOOM_KEY_DOWNARROW;
    case KEY_D:
      return DOOM_KEY_RIGHTARROW;
    case KEY_E:
      return DOOM_KEY_USE;

    case KEY_B:
    case KEY_C:
    case KEY_F ... KEY_R:
    case KEY_T ... KEY_V:
    case KEY_X ... KEY_Z:
      return 'A' + (code - KEY_A);

    case KEY_1 ... KEY_9:
      return '1' + (code - KEY_1);
    case KEY_0:
      return '0';

    case KEY_F1 ... KEY_F12:
      return DOOM_KEY_F1 + (code - KEY_F1);

    case KEY_ENTER:
      return DOOM_KEY_ENTER;
    case KEY_ESCAPE:
      return DOOM_KEY_ESCAPE;
    case KEY_BACKSPACE:
      return DOOM_KEY_BACKSPACE;
    case KEY_TAB:
      return DOOM_KEY_TAB;
    case KEY_SPACE:
      return DOOM_KEY_FIRE;

    case KEY_MINUS:
      return DOOM_KEY_MINUS;
    case KEY_EQUAL:
      return DOOM_KEY_EQUALS;
    case KEY_GRAVE:
      return DOOM_KEY_BACKTICK;

    case KEY_RIGHT:
      return DOOM_KEY_RIGHTARROW;
    case KEY_LEFT:
      return DOOM_KEY_LEFTARROW;
    case KEY_DOWN:
      return DOOM_KEY_DOWNARROW;
    case KEY_UP:
      return DOOM_KEY_UPARROW;

    default:
      return 0;
  }
}

static void queue_key(unsigned char key, int pressed) {
  int next_write = (key_write_idx + 1) % KEY_QUEUE_SIZE;
  if (next_write != key_read_idx) {
    key_queue[key_write_idx].key = key;
    key_queue[key_write_idx].pressed = pressed;
    key_write_idx = next_write;
  }
}

static int dequeue_key(unsigned char *key, int *pressed) {
  if (key_read_idx != key_write_idx) {
    *key = key_queue[key_read_idx].key;
    *pressed = key_queue[key_read_idx].pressed;
    key_read_idx = (key_read_idx + 1) % KEY_QUEUE_SIZE;
    return 1;
  } else {
    // queue is empty
    *key = 0;
    *pressed = 0;
    return 0;
  }
}

static void process_keyboard_events() {
  struct input_event ev;

  while (read(kbd_fd, &ev, sizeof(ev)) == sizeof(ev)) {
    if (ev.type == EV_KEY && ev.code < KEY_MAX) {
      unsigned char doom_key = keycode_to_doom(ev.code);
      if (doom_key) {
        // handle key press/release
        if (ev.value == 0 && key_states[ev.code]) {
          // key release
          queue_key(doom_key, 0);
          key_states[ev.code] = 0;
        } else if (ev.value == 1 && !key_states[ev.code]) {
          // key press
          queue_key(doom_key, 1);
          key_states[ev.code] = 1;
        }
        // value == 2 is key repeat, we ignore it
      }
    }
  }
}

void doomgeneric_Create(int argc, char **argv) {
  // save arguments
  myargc = argc;
  myargv = argv;

  M_FindResponseFile();

  DG_Init();
  D_DoomMain();
}


void DG_Init() {
  int argFPS = M_CheckParmWithArgs("-fps", 1);
  if (argFPS > 0) {
    long fps = strtol(myargv[argFPS + 1], NULL, 10);
    if (fps > 0 && fps <= 60) {
      printf("Target FPS: %u\n", target_fps);
      target_fps = fps;
      target_frame_time = 1000 / target_fps;
    } else {
      fprintf(stderr, "Invalid FPS value: %s. Using default.\n", myargv[argFPS + 1]);
    }
  }

  // check for fullscreen scaling
  if (M_CheckParm("-fullscreen") || M_CheckParm("-scale")) {
    scale_to_fullscreen = 1;
    printf("Scaling to fullscreen enabled\n");
  }

  // open framebuffer
  fb_fd = open("/dev/fb0", O_RDWR);
  if (fb_fd < 0) {
    perror("Error opening framebuffer");
    exit(1);
  }

  // get screen info
  if (ioctl(fb_fd, FBIOGETINFO, &fbinfo) < 0) {
    perror("Error reading information");
    exit(1);
  }

  fb_width = fbinfo.xres;
  fb_height = fbinfo.yres;
  fb_bytesperpixel = fbinfo.bits_per_pixel / 8;
  fb_stride = fb_width * fb_bytesperpixel;

  if (!scale_to_fullscreen) {
    // to center the image on screen
    fb_xoffset = (fb_width - DOOMGENERIC_RESX) / 2;
    fb_yoffset = (fb_height - DOOMGENERIC_RESY) / 2;
  } else {
    // fullscreen scaling - no offset needed
    fb_xoffset = 0;
    fb_yoffset = 0;
  }

  fb_ptr = mmap(0, fbinfo.size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fb_fd, 0);
  if (fb_ptr == MAP_FAILED) {
    perror("Error mapping framebuffer");
    exit(1);
  }

  // allocate screen buffer
  DG_ScreenBuffer = malloc((size_t) DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t));
  if (!DG_ScreenBuffer) {
    perror("Error allocating screen buffer");
    exit(1);
  }

  // open keyboard device
  kbd_fd = open("/dev/events0", O_RDONLY | O_NONBLOCK);
  if (kbd_fd < 0) {
    perror("Error opening keyboard device");
  }
}

_used void DG_DrawFrame() {
  if (!scale_to_fullscreen) {
    // original centered rendering
    for (int line = 0; line < DOOMGENERIC_RESY; line++) {
      memcpy(
        (fb_ptr + (fb_stride * (line + fb_yoffset)) + (fb_xoffset * fb_bytesperpixel)),
        fb_bytesperpixel == 4 ?
        DG_ScreenBuffer + (uintptr_t)(DOOMGENERIC_RESX * line) :
        DG_ScreenBuffer + (uintptr_t)(DOOMGENERIC_RESX * (line / 2)),
        (fb_bytesperpixel * DOOMGENERIC_RESX)
      );
    }
  } else {
    // scaled fullscreen rendering
    float x_scale = (float)fb_width / DOOMGENERIC_RESX;
    float y_scale = (float)fb_height / DOOMGENERIC_RESY;

    for (int fb_y = 0; fb_y < fb_height; fb_y++) {
      int doom_y = (int)((float)fb_y / y_scale);
      if (doom_y >= DOOMGENERIC_RESY) doom_y = DOOMGENERIC_RESY - 1;

      pixel_t *doom_line = DG_ScreenBuffer + ((uintptr_t)doom_y * DOOMGENERIC_RESX);
      char *fb_line = fb_ptr + (fb_y * fb_stride);

      for (int fb_x = 0; fb_x < fb_width; fb_x++) {
        int doom_x = (int)((float)fb_x / x_scale);
        if (doom_x >= DOOMGENERIC_RESX) doom_x = DOOMGENERIC_RESX - 1;

        pixel_t pixel = doom_line[doom_x];
        *((uint32_t*)(fb_line + ((uintptr_t)fb_x * 4))) = pixel;
      }
    }
  }

  process_keyboard_events();
}

_used void DG_SleepMs(uint32_t ms) {
  if (target_fps > 0) {
    usleep(target_frame_time * 1000);
  } else {
    usleep(ms * 1000);
  }
}

_used uint32_t DG_GetTicksMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

_used int DG_GetKey(int *pressed, unsigned char *key) {
  process_keyboard_events();
  return dequeue_key(key, pressed);
}

_used void DG_SetWindowTitle(const char * title) {
  printf("Window Title: %s\n", title);
}

int main(int argc, char **argv) {
  doomgeneric_Create(argc, argv);

  while (1) {
    doomgeneric_Tick();
  }

  return 0;
}
