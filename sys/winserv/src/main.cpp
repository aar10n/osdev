#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <core/Buffer.hpp>
#include <core/Drawable.hpp>

int main() {
  std::cout << "Hello, world!" << std::endl;

  int framebuf = open("/dev/fb0", O_WRONLY);
  if (framebuf < 0) {
    fprintf(stderr, "failed to open /dev/fb0: %s\n", strerror(errno));
  }

  size_t fb_size = 1024 * 600 * sizeof(uint32_t);
  void *fb = mmap(nullptr, fb_size, PROT_WRITE, 0, framebuf, 0);
  if (fb == MAP_FAILED) {
    fprintf(stderr, "failed to mmap framebuffer\n");
    exit(1);
  }
  memset(fb, UINT32_MAX, fb_size);

  Buffer buffer = Buffer(1024, 600, static_cast<uint32_t *>(fb));
  buffer.fill(Color(1, 129, 129).getValueBGR());

  Rectangle rect = Rectangle(Point(100, 100), 640, 480)
    .color(Color(192,192,192));
  buffer.draw(rect);

  return 0;
}
