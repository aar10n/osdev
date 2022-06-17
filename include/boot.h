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


// The bootloader will dynamically link the boot_info pointer
// to variables annotated with the __boot_info attribute.
#define __boot_data __attribute__((section(".boot_data")))


#define KERNEL_MAX_SIZE  SIZE_2MB

// Memory map
#define MEMORY_UNKNOWN          0
#define MEMORY_UNUSABLE         1
#define MEMORY_USABLE           2
#define MEMORY_RESERVED         3
#define MEMORY_ACPI             4
#define MEMORY_ACPI_NVS         5
#define MEMORY_MAPPED_IO        6
#define MEMORY_EFI_RUNTIME_CODE 7
#define MEMORY_EFI_RUNTIME_DATA 8

// Framebuffer pixel format
#define FB_PIXEL_FORMAT_UNKNOWN 0x0
#define FB_PIXEL_FORMAT_RGB     0x1
#define FB_PIXEL_FORMAT_BGR     0x2

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

typedef struct memory_map_entry {
  uint32_t type;
  uint32_t : 32; // reserved
  uint64_t base;
  uint64_t size;
} memory_map_entry_t;

typedef struct memory_map {
  uint32_t size;           // size of the memory map
  uint32_t capacity;       // size allocated for the memory map
  memory_map_entry_t *map; // pointer to the memory map
} memory_map_t;

// The full boot structure

typedef struct boot_info_v2 {
  uint8_t magic[4];               // boot signature ('BOOT')
  // kernel info
  uint32_t kernel_phys_addr;      // kernel physical address
  uint64_t kernel_virt_addr;      // kernel virtual address
  uint32_t kernel_size;           // kernel size in bytes
  uint32_t pml4_addr;             // pml4 table address
  // memory info
  uint64_t mem_total;             // total memory
  memory_map_t mem_map;           // system memory map
  // framebuffer
  uint64_t fb_addr;               // framebuffer base address
  uint64_t fb_size;               // framebuffer size in bytes
  uint32_t fb_width;              // framebuffer width
  uint32_t fb_height;             // framebuffer height
  uint32_t fb_pixel_format;       // framebuffer pixel format
  uint32_t : 32;                  // reserved
  // system configuration
  uint32_t efi_runtime_services;  // EFI Runtime Services table
  uint32_t acpi_ptr;              // ACPI RDSP table address
  uint32_t smbios_ptr;            // SMBIOS Entry Point table address
  uint32_t : 32;                  // reserved
} boot_info_v2_t;

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
