//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

int atoi(char *nptr);
void itoa(int n, char *s, int radix);
void btoa(int n, char *s);
void dtoa(int n, char *s);
void xtoa(int n, char *s);

#endif // LIBC_STDLIB_H
