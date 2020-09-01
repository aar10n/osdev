//
// Created by Aaron Gill-Braun on 2019-04-17.
//

#include <kernel/cpu/asm.h>
#include <drivers/ata.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <drivers/serial.h>
#include <multiboot.h>

#include <fs/ext2/ext2.h>
#include <fs/fs.h>

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/idt.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>

void main(multiboot_info_t *mbinfo) {
  kclear();
  kprintf("Kernel loaded!\n");

  install_gdt();
  install_idt();

  enable_hardware_interrupts();

  init_serial(COM1);
  init_keyboard();
  init_ata();

  kprintf("\n");
  kprintf("Kernel Start: %p (%p)\n", kernel_start, virt_to_phys(kernel_start));
  kprintf("Kernel End: %p (%p)\n", kernel_end, virt_to_phys(kernel_end));
  kprintf("Kernel Size: %d KiB\n", (kernel_end - kernel_start) / 1024);
  kprintf("\n");
  kprintf("Lower Memory: %d KiB\n", mbinfo->mem_lower);
  kprintf("Upper Memory: %d KiB\n", mbinfo->mem_upper);
  kprintf("\n");

  init_paging();

  uintptr_t mem_start = 0x100000;
  // align the kernel_end address
  uintptr_t kernel_aligned = (virt_to_phys(kernel_end) + 0x1000) & 0xFFFF000;
  kprintf("kernel_aligned %p\n", phys_to_virt(kernel_aligned));
  // save 1 MB heap for the "dumb" allocator
  uintptr_t base_addr = kernel_aligned + 0x100000;
  kprintf("start_addr %p\n", phys_to_virt(base_addr));
  size_t mem_size = (mbinfo->mem_upper * 1024) - (base_addr - mem_start);
  kprintf("base address: %p | size: %u MiB\n", base_addr, mem_size / (1024 * 1024));

  // Initialize memory
  mem_init(base_addr, mem_size);

  // page_t *page = alloc_pages(2, 0);
  // mm_print_debug_page(page);

  kheap_init();

  kprintf("pde: %d | pte: %d\n", addr_to_pde(0xC0000041), addr_to_pte(0xC0000041));

#define KMALLOC_TEST(test, size) \
  kprintf("%s - kmalloc(%d)\n", #test, size);\
  void *test = kmalloc(size); \
  kprintf("%s -> 0x%08X\n\n", #test, test);

#define KFREE_TEST(test) \
  kprintf("%s - kmalloc(%s)\n", #test, #test);\
  kfree(test); \
  kprintf("\n");

#define KREALLOC_TEST(test, ptr, size) \
  kprintf("%s - krealloc(%s, %u)\n", #test, #ptr, size); \
  void *test = krealloc(ptr, size); \
  kprintf("%s -> 0x%08X\n\n", #test, test);

  KMALLOC_TEST(test1, 8);
  KMALLOC_TEST(test2, 8);

  KFREE_TEST(test2);

  KMALLOC_TEST(test4, 16);

  KREALLOC_TEST(test5, test1, 12);


  // ata_t disk = { ATA_DRIVE_PRIMARY };
  // ata_info_t disk_info;
  // ata_identify(&disk, &disk_info);
}
