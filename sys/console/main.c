//
// Created by Aaron Gill-Braun on 2021-08-17.
//

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <osdev/event.h>


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

  key_event_t event;
  while ((read(events, &event, sizeof(key_event_t))) > 0) {
    printf("key code: %d | release: %d (%x)\n", event.key_code, event.release, event.modifiers);
    if (event.key_code == VK_KEYCODE_ESCAPE) {
      break;
    }
  }

  return 0;
}
