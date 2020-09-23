//
// Created by Aaron Gill-Braun on 2020-09-12.
//

#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <kernel/mm/paging.h>
#include <stdint.h>

typedef struct task {
  uint32_t pid;
  uintptr_t esp;
  uintptr_t ebp;
  uintptr_t eip;
  pde_t *page_directory;
  struct task *next;
} task_t;

//

void tasking_init();
void task_switch();
int fork();
int getpid();

#endif
