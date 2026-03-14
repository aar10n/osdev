//
// Interval type and utility macros.
//

#ifndef LIB_INTERVAL_H
#define LIB_INTERVAL_H

#include <kernel/base.h>

typedef struct interval {
  uint64_t start;
  uint64_t end;
} interval_t;

#define NULL_SET ((interval_t){ UINT64_MAX, 0 })

#define intvl(start, end) \
  ((interval_t){ start, end })

#define magnitude(i) \
  ((i).end - (i).start)

#define is_null_set(i) \
  ((i).start == UINT64_MAX && (i).end == 0)

#define intvl_eq(i, j) \
  (((i).start == (j).start) && ((i).end == (j).end))

// intersection of i and j
#define intersection(i, j) \
  (((j).start >= (i).end) || ((i).start >= (j).end) ? \
    NULL_SET : intvl(max((i).start, (j).start), min((i).end, (j).end)))

// subtract j from i (i - j)
#define subtract(i, j) \
  ((contains(i, j) || !overlaps(i, j)) ? NULL_SET : (i).start < (j).start ? \
    intvl((i).start, (j).start) : \
    intvl((j).end, (i).end))

// i and j are contiguous
#define contiguous(i, j) \
  (!overlaps(i, j) && (((j).start == (i).end) || ((i).start == (j).end)))

// i contains j
#define contains(i, j) \
  (intvl_eq(intersection(i, j), j))

#define contains_point(i, p) \
  ((p) >= (i).start && (p) < (i).end)

// i and j overlap
#define overlaps(i, j) \
  (!is_null_set(intersection(i, j)))

#endif
