//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <boot.h>
#include <stdio.h>

#include <cpu/cpu.h>
#include <mm/mm.h>

#include <drivers/serial.h>

uintptr_t kernel_phys;

void main(boot_info_t *info) {
  kernel_phys = info->kernel_phys;

  serial_init(COM1);
  kprintf("Kernel loaded!\n");

  // uint64_t entry = virt_addr(511, 510, 0, 0);
  // kprintf("Testing: %p\n", KERNEL_VA);
  // uint64_t one = 0xFFFFFF8000000000;
  // uint64_t two = 0xFFFFFFFF80000000;
  // kprintf("Index: %d | %d\n", pml4_index(one), pdpt_index(one));
  // kprintf("Index: %d | %d\n", pml4_index(one), pdpt_index(two));
  // kprintf("Boot info: %p\n", info);
  // enable_sse();
}
