//
// Created by Aaron Gill-Braun on 2020-09-19.
//

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <kernel/mem/paging.h>
#include <kernel/mem/stack.h>

// static uintptr_t stack_top = 0xC8000000; // 128 MiB
uintptr_t initial_esp = 0;

void relocate_stack(uintptr_t new_stack_top, uint32_t size) {
  // for (uint32_t i = new_stack_top; i >= new_stack_top - size; i -= PAGE_SIZE) {
  //   pte_t pte = virt_to_phys(i - PAGE_SIZE) | PE_READ_WRITE | PE_PRESENT;
  //   map_frame(i, pte);
  // }
  // flush_tlb();

  uint32_t old_stack_pointer;
  __asm volatile("mov %%esp, %0" : "=r" (old_stack_pointer));
  uint32_t old_base_pointer;
  __asm volatile("mov %%ebp, %0" : "=r" (old_base_pointer));

  uint32_t offset = new_stack_top - initial_esp;
  uint32_t new_stack_pointer = old_stack_pointer + offset;
  uint32_t new_base_pointer  = old_base_pointer  + offset;

  uint32_t old_stack_size = initial_esp - old_stack_pointer;
  memcpy((void *) new_stack_pointer, (void *) old_stack_pointer, old_stack_size);

  // offset all references to the old stack to our new stack
  uint32_t *ptr = (uint32_t *) new_stack_pointer;
  for (; ptr < (uint32_t *) new_stack_top; ptr++) {

    uint32_t value = *ptr;
    if (value > old_stack_pointer && value < initial_esp) {
      kprintf("updating reference\n");
      *ptr = value + offset;
    }
  }

  kprintf("changing stack pointers\n");

  // change stack pointers
  __asm volatile("mov %0, %%esp" : : "r" (new_stack_pointer));
  __asm volatile("mov %0, %%ebp" : : "r" (new_base_pointer));
}
