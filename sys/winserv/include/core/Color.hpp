//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#ifndef SYS_WINSERV_CORE_COLOR_HPP
#define SYS_WINSERV_CORE_COLOR_HPP

struct Color {
  uint8_t r, g, b, a;

  Color() : r(0), g(0), b(0), a(0) {};
  Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b), a(255) {};
  Color(uint8_t r, uint8_t g, uint8_t b, float a) : r(r), g(g), b(b), a(a * 255) {};

  uint32_t getValueBGR() const {
    return (b << 0) | (g << 8) | (r << 16) | (a << 24);
  }
};

#endif
