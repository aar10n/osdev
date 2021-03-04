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

#define KERNEL_PA      0x100000
#define KERNEL_OFFSET  0xFFFFFF8000000000
#define KERNEL_VA      0xFFFFFF8000100000
#define STACK_VA       0xFFFFFFA000000000
#define FRAMEBUFFER_VA 0xFFFFFFC000000000
#define MMIO_BASE_VA   0xFFFFFFFFE0000000
#define PROGRAM_VA     0xFFFFFF8000010000

#define SMPBOOT_START  0x0000
#define SMPDATA_START  0x1000

#define STACK_SIZE      0x4000 // 8 KiB
#define KERNEL_RESERVED 0x300000 // 3 MiB
#define RESERVED_TABLES 6 // Number of preallocated page tables

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

// Framebuffer

#define PIXEL_RGB 0
#define PIXEL_BGR 1
#define PIXEL_BITMASK 2

typedef struct {
  uint32_t red_mask;
  uint32_t green_mask;
  uint32_t blue_mask;
  uint32_t reserved_mask;
} pixel_bitmask_t;


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
  uint8_t magic[4];                // 'BOOT' magic
  uintptr_t kernel_phys;           // the kernel physical address
  uint8_t bsp_id;                  // boot processor id
  uint8_t num_cores;               // the number of physical cores
  uint16_t num_threads;            // the number of threads
  // memory info
  memory_map_t *mem_map;           // the memory map array
  uintptr_t pml4;                  // pointer to the pml4 page table
  uintptr_t reserved_base;         // pointer to start of reserved kernel area
  size_t reserved_size;            // the size of reserved kernel area
  // framebuffer info
  uintptr_t fb_base;               // framebuffer base pointer
  size_t fb_size;                  // framebuffer size
  uint32_t fb_width;               // framebuffer width
  uint32_t fb_height;              // framebuffer height
  uint32_t fb_pixels_per_scanline; // frambuffer pixels per scanline
  uint32_t fb_pixel_format;        // format of the pixels
  pixel_bitmask_t fb_pixel_info;   // pixel information bitmask
  // system info
  uintptr_t runtime_services;       // a pointer to the EFI runtime services
  uintptr_t acpi_table;             // a pointer to the ACPI RDSP (if present)
  uintptr_t smbios_table;           // a pointer to the Smbios entry table (if present)
} boot_info_t;

#endif
