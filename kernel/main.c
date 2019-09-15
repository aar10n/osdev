//
// Created by Aaron Gill-Braun on 2019-04-17.
//

#include <drivers/asm.h>
#include <drivers/ata.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <drivers/serial.h>
#include <multiboot.h>

#include <fs/ext2/ext2.h>
#include <fs/fs.h>

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/isr.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/page.h>

void kmain(multiboot_info_t *mbinfo) {
  kclear();
  kprintf("Kernel loaded!\n");

  install_gdt();
  install_isr();

  enable_hardware_interrupts();

  init_serial(COM1);
  init_keyboard();
  init_ata();

  kprintf("\n");
  kprintf("Kernel Start: %p\n", kernel_start);
  kprintf("Kernel End: %p\n", kernel_end);
  kprintf("Kernel Size: %d KiB\n", (kernel_end - kernel_start) / 1024);
  kprintf("\n");
  kprintf("Lower Memory: %d KiB\n", mbinfo->mem_lower);
  kprintf("Upper Memory: %d KiB\n", mbinfo->mem_upper);
  kprintf("\n");

  init_paging();
  memory_map_t *mmap = (memory_map_t *) ptov(mbinfo->mmap_addr);
  int num_entries = mbinfo->mmap_length / sizeof(memory_map_t);
  for (int i = 0; i < num_entries; i++) {
    memory_map_t *mb = &mmap[i];
    if (mb->base_addr_low != 0 && mb->type == MEMORY_AVAILABLE) {
      uintptr_t base_addr = 1 << (log2(mb->base_addr_low) + 1);
      size_t size = mb->length_low - (base_addr - mb->base_addr_low);
      mem_init(base_addr, size);
    }
  }


  ata_t disk = { ATA_DRIVE_PRIMARY };
  ata_info_t disk_info;
  ata_identify(&disk, &disk_info);
}
