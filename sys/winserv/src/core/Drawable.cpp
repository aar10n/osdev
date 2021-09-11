//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#include <core/Drawable.hpp>
#include <core/Buffer.hpp>
#include <cstdlib>
#include <iostream>

static void drawLineLow(Buffer &buffer, int x0, int y0, int x1, int y1, uint32_t v) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int yi = 1;
  if (dy < 0) {
    yi = -1;
    dy = -dy;
  }
  int D = 2 * dy - dx;
  int y = y0;

  for (int x = x0; x <= x1; x++) {
    buffer(x, y) = v;
    if (D > 0) {
      y += yi;
      D += 2 * (dy - dx);
    } else {
      D += 2 * dy;
    }
  }
}

static void drawLineHigh(Buffer &buffer, int x0, int y0, int x1, int y1, uint32_t v) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int xi = 1;
  if (dx < 0) {
    xi = -1;
    dx = -dx;
  }
  int D = 2 * dx - dy;
  int x = x0;

  for (int y = y0; y <= y1; y++) {
    buffer(x, y) = v;
    if (D > 0) {
      x += xi;
      D += 2 * (dx - dy);
    } else {
      D += 2 * dx;
    }
  }
}

static void drawLine(Buffer &buffer, Point a, Point b, Color c) {
  uint32_t value = c.getValueBGR();
  int x0 = a.x;
  int y0 = a.y;
  int x1 = b.x;
  int y1 = b.y;

  double slope = Point::slope(a, b);
  if (slope == 0) {
    // horizontal line
    int start = buffer.toIndex(x0, y0);
    int end = buffer.toIndex(x1, y1);
    for (int i = start; i <= end; i++) {
      buffer[i] = value;
    }
  } else if (slope == NAN) {
    // vertical line
    int stride = buffer.getWidth();
    int start = buffer.toIndex(x0, y0);
    int end = buffer.toIndex(x1, y1);
    for (int i = start; i <= end; i += stride) {
      buffer[i] = value;
    }
  } else if (abs(y1 - y0) < abs(x1 - x0)) {
    if (x0 > x1) {
      drawLineLow(buffer, x1, y1, x0, y0, value);
    } else {
      drawLineLow(buffer, x0, y0, x1, y1, value);
    }
  } else {
    if (y0 > y1) {
      drawLineHigh(buffer, x1, y1, x0, y0, value);
    } else {
      drawLineHigh(buffer, x0, y0, x1, y1, value);
    }
  }
}

// Polygon

void Polygon::draw(Buffer &buffer) {
  for (int i = 0; i < m_points.size(); i++) {
    if (i == 0) {
      continue;
    }
    drawLine(buffer, m_points[i - 1], m_points[i], m_color);
  }
}

// Line

void Line::draw(Buffer &buffer) {
  drawLine(buffer, m_start, m_end, m_color);
}

// DecoratedLine



// FilledRectangle

void FilledRectangle::draw(Buffer &buffer) {
  int x0 = m_origin.x;
  int y0 = m_origin.y;
  int x1 = x0 + m_width;
  int y1 = y0 + m_height;

  int width = buffer.getWidth();
  uint32_t value = m_color.getValueBGR();
  for (int y = y0; y <= y1; y++) {
    buffer.fill(y * width + x0, y * width + x1, value);
  }
}

// Rectangle

void Rectangle::draw(Buffer &buffer) {
  int x0 = m_origin.x;
  int y0 = m_origin.y;
  int x1 = x0 + m_width;
  int y1 = y0 + m_height;

  int width = buffer.getWidth();
  uint32_t value = m_color.getValueBGR();
  for (int y = y0; y <= y1; y++) {
    buffer.fill(y * width + x0, y * width + x1, value);
  }

  Color dark = Color(54, 56, 54);
  Color light = Color(224, 224, 224);

  FilledRectangle left(Point(x0 - 2, y0 - 2), 2, m_height + 2, light);
  FilledRectangle right(Point(x1 + 1, y0 - 2), 2, m_height + 2, dark);
  FilledRectangle top(Point(x0 - 2, y0 - 2), m_width + 2, 2, light);
  FilledRectangle bottom(Point(x0 + 2, y1 + 2), m_width + 2, 2, dark);

  buffer.draw(left);
  buffer.draw(right);
  buffer.draw(top);
  buffer.draw(bottom);
}
