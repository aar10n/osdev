//
// Created by Aaron Gill-Braun on 2022-06-08.
//

#include <mm/init.h>
#include <mm/heap.h>
#include <mm/pgtable.h>

#include <cpu/cpu.h>
#include <printf.h>
#include <panic.h>

uintptr_t kernel_address;
uintptr_t kernel_virtual_offset;
uintptr_t kernel_code_start;
uintptr_t kernel_code_end;
uintptr_t kernel_data_end;

uintptr_t kernel_reserved_start;
uintptr_t kernel_reserved_end;
uintptr_t kernel_reserved_ptr;
memory_map_entry_t *reserved_map_entry;

void mm_early_init() {
  kernel_address = (uintptr_t) &__kernel_code_start;
  kernel_virtual_offset = (uintptr_t) &__kernel_virtual_offset;
  kernel_code_start = (uintptr_t) &__kernel_code_start;
  kernel_code_end = (uintptr_t) &__kernel_code_end;
  kernel_data_end = (uintptr_t) &__kernel_data_end;
  early_init_pgtable();

  size_t usable_mem_size = 0;
  memory_map_entry_t *kernel_entry = NULL;
  memory_map_entry_t *kernel_reserved_entry = NULL;
  uintptr_t kernel_start_phys = boot_info_v2->kernel_phys_addr;
  uintptr_t kernel_end_phys = kernel_start_phys + boot_info_v2->kernel_size;

  kprintf("boot memory map:\n");
  memory_map_t *memory_map = &boot_info_v2->mem_map;
  size_t num_entries = memory_map->size / sizeof(memory_map_entry_t);
  for (size_t i = 0; i < num_entries; i++) {
    memory_map_entry_t *entry = &memory_map->map[i];
    uintptr_t start = entry->base;
    uintptr_t end = entry->base + entry->size;

    const char *type = NULL;
    switch (entry->type) {
      case MEMORY_UNKNOWN: type = "unknown"; break;
      case MEMORY_UNUSABLE: type = "unusable"; break;
      case MEMORY_USABLE: type = "usable"; break;
      case MEMORY_RESERVED: type = "reserved"; break;
      case MEMORY_ACPI: type = "ACPI data"; break;
      case MEMORY_ACPI_NVS: type = "ACPI NVS"; break;
      case MEMORY_MAPPED_IO: type = "mapped io"; break;
      case MEMORY_EFI_RUNTIME_CODE: type = "EFI runtime code"; break;
      case MEMORY_EFI_RUNTIME_DATA: type = "EFI runtime data"; break;
      default: panic("bad memory map");
    }

    if (entry->type == MEMORY_USABLE) {
      usable_mem_size += entry->size;
      // pick suitable range for kernel heap + reserved memory
      if (kernel_reserved_entry == NULL && start >= SIZE_16MB && entry->size >= SIZE_8MB) {
        kernel_reserved_entry = entry;
      }
    }

    if (kernel_start_phys >= start && kernel_end_phys <= end) {
      kassert(kernel_entry == NULL);
      kassert(kernel_start_phys == start);
      kernel_entry = entry;
    }

    kprintf("  [%016p-%016p] %s (%M)\n", start, end, type, entry->size);
  }

  kprintf("total memory: %M\n", boot_info_v2->mem_total);
  kprintf("usable memory: %M\n", usable_mem_size);
  kassert(kernel_entry != NULL);
  kassert(kernel_reserved_entry != NULL);

  kernel_entry->base = kernel_end_phys;
  kernel_entry->size -= boot_info_v2->kernel_size;

  kernel_reserved_start = kernel_reserved_entry->base;
  kernel_reserved_end = kernel_reserved_start + kernel_reserved_entry->size;
  kernel_reserved_ptr = kernel_reserved_start;
  reserved_map_entry = kernel_reserved_entry;

  early_init_pgtable();
  mm_init_kheap();
}

uintptr_t mm_early_alloc_pages(size_t count) {
  uintptr_t addr = kernel_reserved_ptr;
  size_t size = count * PAGE_SIZE;

  kernel_reserved_ptr += size;
  if (kernel_reserved_ptr > kernel_reserved_end) {
    panic("out of reserved memory");
  }

  reserved_map_entry->base = kernel_reserved_ptr;
  reserved_map_entry->size -= size;
  return addr;
}
