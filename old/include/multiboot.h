#ifndef LIBC_MULTIBOOT_H
#define LIBC_MULTIBOOT_H

#include <stdint.h>

#define MB_MEMORY_AVAILABLE 1
#define MB_MEMORY_RESERVED  2
#define MB_MEMORY_ACPI      3
#define MB_MEMORY_NVS       4
#define MB_MEMORY_BADRAM    5


/* The Multiboot header. */
typedef struct multiboot_header {
  uint32_t magic;
  uint32_t flags;
  uint32_t checksum;

  // These are only valid if MULTIBOOT_AOUT_KLUDGE is set.
  uint32_t header_addr;
  uint32_t load_addr;
  uint32_t load_end_addr;
  uint32_t bss_end_addr;
  uint32_t entry_addr;

  // These are only valid if MULTIBOOT_VIDEO_MODE is set.
  uint32_t mode_type;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} multiboot_header_t;

/* The symbol table for a.out. */
typedef struct aout_symbol_table {
  uint32_t tabsize;
  uint32_t strsize;
  uint32_t addr;
  uint32_t reserved;
} aout_symbol_table_t;

/* The section header table for ELF. */
typedef struct {
  uint32_t num;
  uint32_t size;
  uint32_t addr;
  uint32_t shndx;
} elf_section_header_table_t;

/* The Multiboot information. */
typedef struct multiboot_info {
  // Multiboot info version number
  uint32_t flags;
  // Available memory from BIOS
  uint32_t mem_lower;
  uint32_t mem_upper;
  // Root partition
  uint32_t boot_device;
  // Kernel command line
  uint32_t cmdline;
  // Boot module list
  uint32_t mods_count;
  uint32_t mods_addr;

  union {
    aout_symbol_table_t aout_sym;
    elf_section_header_table_t elf_sec;
  } u;

  // Memory mapping buffer
  uint32_t mmap_length;
  uint32_t mmap_addr;

  // Drive info buffer
  uint32_t drives_length;
  uint32_t drives_addr;

  // ROM configuration table
  uint32_t config_table;

  // Boot loader Name
  uint32_t boot_loader_name;

  // APM table
  uint32_t apm_table;

  // Video info
  uint32_t vbe_control_info;
  uint32_t vbe_mode_info;
  uint16_t vbe_mode;
  uint16_t vbe_interface_seg;
  uint16_t vbe_interface_off;
  uint16_t vbe_interface_len;

  uint32_t framebuffer_addr_low;
  uint32_t framebuffer_addr_high;
  uint32_t framebuffer_pitch;
  uint32_t framebuffer_width;
  uint32_t framebuffer_height;
  uint8_t framebuffer_bpp;
#define MB_FRAMEBUFFER_TYPE_INDEXED 0
#define MB_FRAMEBUFFER_TYPE_RGB     1
#define MB_FRAMEBUFFER_TYPE_EGA_TEXT     2
  uint8_t framebuffer_type;
  union {
    struct {
      uint32_t framebuffer_palette_addr;
      uint16_t framebuffer_palette_num_colors;
    };
    struct
    {
      uint8_t framebuffer_red_field_position;
      uint8_t framebuffer_red_mask_size;
      uint8_t framebuffer_green_field_position;
      uint8_t framebuffer_green_mask_size;
      uint8_t framebuffer_blue_field_position;
      uint8_t framebuffer_blue_mask_size;
    };
  };
} multiboot_info_t;

/* The module structure. */
typedef struct module {
  uint32_t mod_start;
  uint32_t mod_end;
  uint32_t string;
  uint32_t reserved;
} mb_module_t;

typedef struct {
  uint32_t size;
  uint32_t base_addr_low;
  uint32_t base_addr_high;
  uint32_t length_low;
  uint32_t length_high;
  uint32_t type;
} mb_mmap_entry_t;

#endif // LIBC_MULTIBOOT_H
