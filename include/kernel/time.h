//
// Created by Aaron Gill-Braun on 2023-12-29.
//

#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include <kernel/base.h>

#include <abi/time.h>

uint64_t tm2posix(struct tm *tm);
void posix2tm(uint64_t epoch, struct tm *tm);

static inline bool timeval_is_zero(struct timeval *tv) { return (tv->tv_sec == 0 && tv->tv_usec == 0); }
static inline uint64_t timeval_to_nanos(struct timeval *tv) { return SEC_TO_NS(tv->tv_sec) + US_TO_NS(tv->tv_usec); }
struct timeval timeval_diff(struct timeval *start, struct timeval *end);
struct timeval timeval_add(struct timeval *start, struct timeval *end);


#endif
