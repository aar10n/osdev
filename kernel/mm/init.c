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
uintptr_t kernel_reserved_va_ptr;
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

  uintptr_t initrd_phys = boot_info_v2->initrd_addr;
  size_t initrd_size = boot_info_v2->initrd_size;

  kprintf("boot memory map:\n");
  memory_map_t *memory_map = &boot_info_v2->mem_map;
  size_t num_entries = memory_map->size / sizeof(memory_map_entry_t);
  for (size_t i = 0; i < num_entries; i++) {
    memory_map_entry_t *entry = &memory_map->map[i];
    size_t size = entry->size;
    uintptr_t start = entry->base;
    uintptr_t end = start + size;

    const char *type = NULL;
    switch (entry->type) {
      case MEMORY_UNKNOWN: type = "unknown"; break;
      case MEMORY_UNUSABLE: type = "unusable"; break;
      case MEMORY_USABLE: type = "usable"; break;
      case MEMORY_RESERVED: type = "reserved"; break;
      case MEMORY_ACPI: type = "ACPI data"; break;
      case MEMORY_ACPI_NVS: type = "ACPI NVS"; break;
      case MEMORY_MAPPED_IO: type = "memory mapped io"; break;
      case MEMORY_EFI_RUNTIME_CODE: type = "EFI runtime code"; break;
      case MEMORY_EFI_RUNTIME_DATA: type = "EFI runtime data"; break;
      default: panic("bad memory map");
    }

    if (entry->type == MEMORY_USABLE) {
      usable_mem_size += size;
      // pick suitable range for kernel heap + reserved memory
      if (kernel_reserved_entry == NULL && start >= SIZE_16MB && entry->size >= SIZE_8MB) {
        kernel_reserved_entry = entry;
        kernel_reserved_start = start;
        kernel_reserved_end = end;
        kernel_reserved_ptr = start;
        kernel_reserved_va_ptr = KERNEL_RESERVED_VA;

        // shrink the entry if the start of it overlaps with the loaded initrd image
        // TODO: this is a hack, find a better way to reserve arbitrary memory ranges
        //       during early boot
        if (initrd_phys != 0 && start == initrd_phys) {
          kernel_reserved_start += initrd_size;
          kernel_reserved_ptr += initrd_size;
        }
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
  reserved_map_entry = kernel_reserved_entry;

  kprintf("initrd: %p, %M\n", initrd_phys, initrd_size);
  kprintf("kernel_reserved_start: %p\n", kernel_reserved_start);
  kprintf("kernel_reserved_end: %p\n", kernel_reserved_end);

  if (initrd_phys != 0) {
    // ensure initrd is not in the kernel reserved range
    uintptr_t initrd_end = initrd_phys + initrd_size;
    kassert((initrd_phys < kernel_reserved_start && initrd_end <= kernel_reserved_start) ||
            initrd_phys >= kernel_reserved_end);
  }

  early_init_pgtable();
  mm_init_kheap();
}

void mm_early_reserve_pages(size_t count) {
  reserved_map_entry->base += PAGES_TO_SIZE(count);
  reserved_map_entry->size -= PAGES_TO_SIZE(count);
  if (reserved_map_entry->base > kernel_reserved_end) {
    panic("out of reserved memory");
  }
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

void *mm_early_map_pages_reserved(uintptr_t phys_addr, size_t count, uint32_t flags) {
  uintptr_t va_ptr = kernel_reserved_va_ptr;
  size_t size = pg_flags_to_size(flags) * count;
  kernel_reserved_va_ptr += size;
  return early_map_entries(va_ptr, phys_addr, count, flags);
}
