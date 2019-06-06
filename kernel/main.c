//
// Created by Aaron Gill-Braun on 2019-04-17.
//

#include <multiboot.h>
#include <drivers/ata.h>
#include <drivers/asm.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <drivers/serial.h>

#include <kernel/cpu/isr.h>
#include <kernel/cpu/gdt.h>
#include <kernel/cpu/timer.h>

#include <fs/ext2/ext2.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mem/mem.h"

void kmain(multiboot_info_t *mbinfo) {
  kclear();
  kprintf("Kernel loaded!\n");
  install_gdt();
  isr_install();
  enable_hardware_interrupts();
  init_serial(COM1);
  init_keyboard();
  init_ata();

  kprintf("\n");
  kprintf("Kernel Start: %p\n", kernel_start);
  kprintf("Kernel End: %p\n", kernel_end);
  kprintf("Kernel Size: %d KiB\n",
      (kernel_end - kernel_start) / 1024);
  kprintf("\n");
  kprintf("Lower Memory: %d KiB\n", mbinfo->mem_lower);
  kprintf("Upper Memory: %d KiB\n", mbinfo->mem_upper);
  kprintf("\n");

  memory_map_t *mmap = (memory_map_t *) ptov(mbinfo->mmap_addr);
  int num_entries = mbinfo->mmap_length / sizeof(memory_map_t);
  for (int i = 0; i < num_entries; i++) {
    memory_map_t *mb = &mmap[i];
    if (mb->base_addr_low != 0 && mb->type == MEMORY_AVAILABLE) {
      mem_init(mb->base_addr_low, mb->length_low);
    }
  }

  page_t *page = alloc_pages(1, 3);
  kprintf("page = { \n"
          "  frame = %p\n"
          "  size = %u\n"
          "  flags = %b\n"
          "  next = %p\n"
          "}\n",
          page->frame,
          page->size,
          page->flags,
          page->next);
}
