//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include <Uefi.h>


#define KERNEL_VA 0xFFFFFFFF80000000


// Contains definitions for the data structures given
// to the kernel by the bootloader. The data structures
// include information about system memory, devices

// Memory map
#define MEMORY_NONE     0
#define MEMORY_RESERVED 1
#define MEMORY_FREE     2
#define MEMORY_ACPI     3
#define MEMORY_MMIO     4

#define BOOT_MAGIC "BOOT"
#define BOOT_MAGIC0 'B'
#define BOOT_MAGIC1 'O'
#define BOOT_MAGIC2 'O'
#define BOOT_MAGIC3 'T'

// Memory Map

typedef struct {
  uint32_t type;
  uintptr_t phys_addr;
  size_t size;
} memory_map_t;

// The full boot structure

typedef struct {
  uint8_t magic[4];         // 'BOOT' magic
  // memory info
  size_t mem_total;         // the total amount of physical memory
  memory_map_t *mmap;       // the memory map array
  size_t mmap_size;         // the size of the memory map
  // framebuffer info
  uintptr_t fb_ptr;         // framebuffer pointer
  size_t fb_size;           // framebuffer size
  uint32_t fb_width;        // framebuffer width
  uint32_t fb_height;       // framebuffer height
  uint32_t fb_pps;          // frambuffer pixels per scanline
  // misc info
  EFI_RUNTIME_SERVICES *rt; // a pointer to the EFI runtime services
} boot_info_t;

#endif
