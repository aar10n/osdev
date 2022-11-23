//
// Created by Aaron Gill-Braun on 2022-11-22.
//

#ifndef LIB_SORT_H
#define LIB_SORT_H

#include <base.h>

void qsort(void *base, size_t nitems, size_t size, int (*comp)(const void *, const void*));

int cmp_int(const void *a, const void *b);
int cmp_str(const void *a, const void *b);

#endif
