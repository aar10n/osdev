//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stddef.h>
#include <stdint.h>

// Contains definitions for the data structures given
// to the kernel by the bootloader. The data structures
// include information about system memory, devices

// Memory map
#define MEMORY_UNKNOWN  0
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
} memory_region_t;

typedef struct {
  size_t mem_total;
  size_t mmap_size;
  size_t mmap_capacity;
  memory_region_t *mmap;
} memory_map_t;

// The full boot structure

typedef struct {
  uint8_t magic[4];      // 'BOOT' magic
  uintptr_t kernel_phys; // the kernel physical address
  // memory info
  memory_map_t *mem_map; // the memory map array
  uintptr_t pml4;        // pointer to the pml4 page table
  uintptr_t resrv_start; // pointer to start of reserved kernel region
  size_t resrv_size;     // the size of reserved kernel area
  // framebuffer info
  uintptr_t fb_ptr;      // framebuffer pointer
  size_t fb_size;        // framebuffer size
  uint32_t fb_width;     // framebuffer width
  uint32_t fb_height;    // framebuffer height
  uint32_t fb_pps;       // frambuffer pixels per scanline
  // system info
  uintptr_t efi_rt;      // a pointer to the EFI runtime services
  uintptr_t acpi;        // a pointer to the ACPI RDSP (if present)
  uintptr_t smbios;      // a pointer to the Smbios entry table (if present)
} boot_info_t;

#endif
