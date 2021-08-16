//
// Created by Aaron Gill-Braun on 2020-10-28.
//

#ifndef LIBC_ERRNO_H
#define LIBC_ERRNO_H

#include <abi/errno.h>

const char *strerror(int errnum);

#endif
