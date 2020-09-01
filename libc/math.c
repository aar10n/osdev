//
// Created by Aaron Gill-Braun on 2019-05-22.
//

#include "math.h"

#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
static const char log2_lookup[256] = {
  -1,    0,     1,     1,     2,     2,     2,     2,     3,     3,     3,
  3,     3,     3,     3,     3,     LT(4), LT(5), LT(5), LT(6), LT(6), LT(6),
  LT(6), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7),
};


int abs(int j) {
  return j < 0 ? -j : j;
}

int log2(unsigned int v) {
  unsigned int r;
  register unsigned int t, tt;

  if ((tt = v >> 16)) {
    r = (t = tt >> 8) ? 24 + log2_lookup[t] : 16 + log2_lookup[tt];
  } else {
    r = (t = v >> 8) ? 8 + log2_lookup[t] : log2_lookup[v];
  }

  return r;
}

unsigned int next_pow2(unsigned int v) {
  return 1U << (log2(v - 1) + 1);
}

double pow(double x, double y) {
  if (x == 0) return 0;
  if (y == 0) return 1;

  if (fmod(y, 1) == 0) {
    double value = 0;
    while (y) {
      value *= x;
      if (y < 0) {
        y++;
      } else {
        y--;
      }
    }
    return value;
  }
  return -1;
}

double fmod(double x, double y) {
  return x - ((int) x / y) * y;
}

int imax(int x, int y) {
  return x > y ? x : y;
}

unsigned int umax(unsigned int x, unsigned int y) {
  return x > y ? x : y;
}

int imin(int x, int y) {
  return x > y ? y : x;
}

int umin(unsigned int x, unsigned int y) {
  return x > y ? y : x;
}
