//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#include <core/Buffer.hpp>
#include <stdexcept>
#include <sys/mman.h>
#include <cstring>


Buffer::Buffer(uint32_t width, uint32_t height) :
  m_size(width * height), m_width(width), m_height(height), m_temp(0) {
  void *buffer = mmap(nullptr, m_size * sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
  if (buffer == MAP_FAILED) {
    throw std::runtime_error("failed to allocate window buffer");
  }
  m_buffer = static_cast<uint32_t *>(buffer);
}

Buffer::Buffer(uint32_t width, uint32_t height, uint32_t *buffer) :
  m_buffer(buffer), m_size(width * height),
  m_width(width), m_height(height), m_temp(0) {}

Buffer::~Buffer() {
  munmap(m_buffer, m_size * sizeof(uint32_t));
}

void Buffer::fill(uint32_t value) {
  for (size_t i = 0; i < m_size; i++) {
    m_buffer[i] = value;
  }
}

void Buffer::fill(int start, int end, uint32_t value) {
  if (start >= m_size || start == end) {
    return;
  }

  end = std::min(static_cast<size_t>(end), m_size - 1);
  for (size_t i = start; i <= end; i++) {
    m_buffer[i] = value;
  }
}

void Buffer::draw(Drawable& object) {
  object.draw(*this);
}
