//
// Created by Aaron Gill-Braun on 2025-05-30.
//

#ifndef INCLUDE_ABI_TERMIOS_H
#define INCLUDE_ABI_TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 32

#include <bits/termios.h>

struct winsize {
  unsigned short	ws_row;		  // rows in characters
  unsigned short	ws_col;	    // columns in characters
  unsigned short	ws_xpixel;	// horizontal size in pixels
  unsigned short	ws_ypixel;	// vertical size in pixels
};

#endif
