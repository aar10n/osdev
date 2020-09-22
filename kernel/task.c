//
// Created by Aaron Gill-Braun on 2020-09-12.
//

#include <string.h>

#include <kernel/cpu/asm.h>
#include <kernel/mem/paging.h>
#include <kernel/mem/heap.h>
#include <kernel/task.h>

#include <stdio.h>

volatile task_t *current_task = NULL;
volatile task_t *queue = NULL;
volatile task_t *queue_end = NULL;

int next_pid = 1;

//

void perform_task_switch(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

// Tasking functions

void tasking_init() {
  disable_interrupts();
  kprintf("initializing tasking\n");

  // 8 KiB stack
  // relocate_stack(stack_top, 0x2000);

  // initialize the kernel task
  task_t *task = kmalloc(sizeof(task_t));
  task->pid = next_pid++;
  task->esp = 0;
  task->ebp = 0;
  task->page_directory = current_pd_ptr;
  task->next = NULL;

  queue = task;
  queue_end = task;

  kprintf("tasking initialized!\n");
  enable_interrupts();
}

void task_switch() {
  if (current_task == NULL) {
    return;
  }

  kprintf("switching tasks\n");

  uintptr_t esp;
  uintptr_t ebp;
  uintptr_t eip;
  __asm volatile("mov %%esp, %0" : "=r"(esp));
  __asm volatile("mov %%ebp, %0" : "=r"(ebp));
  eip = get_eip();

  if (eip == 0x12345) {
    // this prevents a loop from happening when the
    // process finishes and jumps back to the saved
    // instruction pointer - which should be located
    // just above
    return;
  }

  current_task->eip = eip;
  current_task->esp = esp;
  current_task->ebp = ebp;

  current_task = current_task->next;
  if (current_task == NULL) {
    current_task = queue;
  }

  perform_task_switch(eip, esp, ebp, virt_to_phys(current_pd));
}

int fork() {
  disable_interrupts();

  volatile task_t *parent = current_task;
  pde_t *pde = clone_page_directory(current_pd_ptr);

  kprintf("forking task %d\n", parent->pid);

  task_t *child = kmalloc(sizeof(task_t));
  child->pid = next_pid++;
  child->esp = 0;
  child->ebp = 0;
  child->page_directory = pde;
  child->next = NULL;

  queue_end->next = child;
  queue_end = child;

  uintptr_t eip = get_eip();
  if (current_task == parent) {
    uintptr_t esp = get_esp();
    uintptr_t ebp = get_ebp();
    child->esp = esp;
    child->ebp = ebp;
    child->eip = eip;

    enable_interrupts();
    return child->pid;
  } else {
    return 0;
  }
}

int getpid() {
  return next_pid - 1;
}
