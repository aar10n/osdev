//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#ifndef SYS_WINSERV_CORE_DRAWABLE_HPP
#define SYS_WINSERV_CORE_DRAWABLE_HPP

#include <core/Geometry.hpp>
#include <core/Color.hpp>
#include <cstdlib>
#include <vector>

class Buffer;

class Drawable {
public:
  virtual void draw(Buffer &buffer) = 0;
};

// Polygon

class Polygon: public Drawable {
private:
  std::vector<Point> m_points;
  Color m_color;

public:
  explicit Polygon(std::vector<Point> points) : m_points(std::move(points)), m_color(Color(0, 0, 0)) {}
  Polygon(std::vector<Point> points, Color color) : m_points(std::move(points)), m_color(color) {}

  void draw(Buffer &buffer) override;
};

// Line

class Line: public Drawable {
private:
  Point m_start;
  Point m_end;
  Color m_color;

public:
  Line(Point start, Point end) :
    m_start(start), m_end(end), m_color(Color(0, 0, 0)) {}
  Line(Point start, Point end, Color color) :
    m_start(start), m_end(end), m_color(color) {}

  void draw(Buffer &buffer) override;
};

// FilledRectangle

class FilledRectangle: public Drawable {
private:
  Point m_origin;
  int m_width;
  int m_height;
  Color m_color;

public:
  FilledRectangle(Point origin, int width, int height) :
    m_origin(origin), m_width(abs(width)), m_height(abs(height)), m_color(Color(0, 0, 0)) {}
  FilledRectangle(Point origin, int width, int height, Color color) :
    m_origin(origin), m_width(abs(width)), m_height(abs(height)), m_color(color) {}

  void draw(Buffer &buffer) override;
};

// Rectangle

class Rectangle: public Drawable {
private:
  Point m_origin;
  int m_width;
  int m_height;
  Color m_color;

public:
  Rectangle(Point origin, int width, int height) :
    m_origin(origin), m_width(abs(width)), m_height(abs(height)), m_color(Color(0, 0, 0)) {}

  Rectangle &color(Color color) { m_color = color; return *this; }

  void draw(Buffer &buffer) override;
};

#endif
