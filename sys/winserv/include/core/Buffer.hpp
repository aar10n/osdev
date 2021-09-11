//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#ifndef SYS_WINSERV_CORE_BUFFER_HPP
#define SYS_WINSERV_CORE_BUFFER_HPP

#include <cstdint>
#include <cstddef>
#include <core/Drawable.hpp>
#include <core/Geometry.hpp>

class Buffer {
private:
  uint32_t *m_buffer;
  size_t m_size;
  uint32_t m_width;
  uint32_t m_height;
  uint32_t m_temp;

public:
  Buffer(uint32_t width, uint32_t height);
  Buffer(uint32_t width, uint32_t height, uint32_t *buffer);
  ~Buffer();

  uint32_t getWidth() const { return m_width; }
  uint32_t getHeight() const { return m_height; }

  void fill(uint32_t value);
  void fill(int start, int end, uint32_t value);
  void draw(Drawable &object);

  uint32_t& operator()(const uint32_t x, const uint32_t y) {
    if (x >= m_width || y >= m_height) {
      return m_temp;
    }
    return m_buffer[y * m_width + x];
  }

  uint32_t& operator[](const uint32_t index) {
    if (index >= m_size) {
      return m_temp;
    }
    return m_buffer[index];
  }

  inline int toIndex(int x, int y) const {
    return y * m_width + x;
  }
};

#endif
