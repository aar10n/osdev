//
// Created by Aaron Gill-Braun on 2022-11-22.
//

#include "sort.h"

#include <string.h>

void qsort(void *base, size_t nitems, size_t size, int (*comp)(const void *, const void*)) {
#define A(idx) (base + ((idx) * size))
  if (nitems < 2) {
    return;
  }

  size_t i;
  size_t j;
  void *pivot = A(nitems / 2);
  for (i = 0, j = nitems - 1; ; i++, j--) {
    while (comp(pivot, A(i)) > 0) i++;
    while (comp(pivot, A(j)) < 0) j--;
    if (i >= j) break;

    uint8_t temp[size];
    memcpy(temp, A(i), size);
    memcpy(A(i), A(j), size);
    memcpy(A(j), temp, size);
  }

  qsort(base, i, size, comp);
  qsort(A(i), nitems - i, size, comp);
#undef A
}

void qsort_int(int *array, size_t len) {
  if (len < 2) {
    return;
  }

  size_t i;
  size_t j;
  int pivot = array[len / 2];
  for (i = 0, j = len - 1; ; i++, j--) {
    while (array[i] < pivot) i++;
    while (array[j] > pivot) j--;
    if (i >= j) break;

    int temp = array[i];
    array[i] = array[j];
    array[j] = temp;
  }

  qsort_int(array, i);
  qsort_int(array + i, len - 1);
}

//
// MARK: Comparison Functions
//

int cmp_int(const void *a, const void *b) {
  return (*(int *) b) - (*(int *) a);
}

int cmp_str(const void *a, const void *b) {
  return strcmp(a, b);
}
