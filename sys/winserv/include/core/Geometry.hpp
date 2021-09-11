//
// Created by Aaron Gill-Braun on 2021-09-10.
//

#ifndef SYS_WINSERV_SHARED_GEOMETRY_HPP
#define SYS_WINSERV_SHARED_GEOMETRY_HPP

#include <cstdint>
#include <cmath>
#include <ostream>

struct Point {
  int x;
  int y;

  Point(int x, int y) : x(x), y(y) {};

  friend std::ostream &operator<<(std::ostream &os, const Point &point) {
    os << "( " << point.x << ", " << point.y << ")";
    return os;
  }

  static double dist(Point a, Point b) {
    return sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
  }
  static double slope(Point a, Point b) {
    double d = b.x - a.x;
    return d == 0 ? NAN : (b.y - a.y) / d;
  }
};


#endif
