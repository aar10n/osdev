//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef DRIVERS_SCREEN_H
#define DRIVERS_SCREEN_H

#define VIDEO_ADDRESS ((char *) 0xC00B8000)
#define MAX_ROWS 25
#define MAX_COLS 80

/* Screen i/o ports */
#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

void screen_print(char *s);
void screen_print_char(char c);
void screen_clear();

void kputc(char c);
void kputs(char *s);
void kclear();

#endif // DRIVERS_SCREEN_H
