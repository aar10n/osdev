//
// Created by Aaron Gill-Braun on 2020-10-28.
//

#ifndef LIBC_ERRNO_H
#define LIBC_ERRNO_H

#include <bits/errno.h>

#define ERRNO_MAX EHWPOISON

const char *strerror(int errnum);

#endif
