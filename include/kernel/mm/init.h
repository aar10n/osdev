//
// Created by Aaron Gill-Braun on 2022-06-08.
//

#ifndef KERNEL_MM_INIT_H
#define KERNEL_MM_INIT_H

#include <base.h>
#include <queue.h>

typedef struct mm_callback {
  void (*callback)(void *);
  void *data;
  LIST_ENTRY(struct mm_callback) list;
} mm_callback_t;


void mm_early_init();
uintptr_t mm_early_alloc_pages(size_t count);
void *mm_early_map_pages(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint16_t flags);

int mm_register_mm_init_callback(void (*callback)(void *data), void *data);
int mm_register_vm_init_callback(void (*callback)(void *data), void *data);

#endif
