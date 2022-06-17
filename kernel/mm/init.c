//
// Created by Aaron Gill-Braun on 2022-06-08.
//

#include <mm/init.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/heap.h>

#include <cpu/cpu.h>
#include <printf.h>
#include <panic.h>
#include <errno.h>

uintptr_t kernel_address;
uintptr_t kernel_virtual_offset;
uintptr_t kernel_code_start;
uintptr_t kernel_code_end;
uintptr_t kernel_data_end;

uintptr_t kernel_reserved_start;
uintptr_t kernel_reserved_end;
uintptr_t kernel_reserved_ptr;
memory_map_entry_t *reserved_map_entry;

LIST_HEAD(mm_callback_t) mm_init_callbacks;
LIST_HEAD(mm_callback_t) vm_init_callbacks;

void mm_early_init() {
  kernel_address = (uintptr_t) &__kernel_code_start;
  kernel_virtual_offset = (uintptr_t) &__kernel_virtual_offset;
  kernel_code_start = (uintptr_t) &__kernel_code_start;
  kernel_code_end = (uintptr_t) &__kernel_code_end;
  kernel_data_end = (uintptr_t) &__kernel_data_end;
  LIST_INIT(&mm_init_callbacks);
  LIST_INIT(&vm_init_callbacks);

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

  // initialize kernel heap
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

void *mm_early_map_pages(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint16_t flags) {
  kassert(virt_addr % PAGE_SIZE == 0);
  kassert(phys_addr % PAGE_SIZE == 0);
  kassert(count > 0);

  int map_level = 1;
  size_t stride = PAGE_SIZE;
  if (flags & PE_2MB_SIZE) {
    kassert(is_aligned(virt_addr, SIZE_2MB));
    kassert(is_aligned(phys_addr, SIZE_2MB));
    map_level = 2;
    stride = SIZE_2MB;
    flags |= PE_SIZE;
  } else if (flags & PE_1GB_SIZE) {
    kassert(is_aligned(virt_addr, SIZE_1GB));
    kassert(is_aligned(phys_addr, SIZE_1GB));
    map_level = 3;
    stride = SIZE_1GB;
    flags |= PE_SIZE;
    if (!cpu_query_feature(CPU_BIT_PDPE1GB)) {
      kprintf("warning: 1GB mapping requested but not supported by CPU\n"
              "         using 2MB mapping instead\n");
      map_level = 2;
      stride = SIZE_2MB;
      count = (count * SIZE_1GB) / SIZE_2MB;
    }
  }

  void *addr = (void *) virt_addr;
  while (count > 0) {
    uint64_t *pml4 = (void *) ((uint64_t) boot_info_v2->pml4_addr);
    uint64_t *table = pml4;
    int index;
    for (int i = 4; i > 0; i--) {
      index = (virt_addr >> page_level_to_shift(i)) & 0x1FF;
      if (i == map_level) {
        for (; index < 512; index++) {
          table[index] = phys_addr | (flags & PAGE_FLAGS_MASK) | PE_PRESENT;
          virt_addr += stride;
          phys_addr += stride;
          count--;
        }
        continue;
      }

      uintptr_t next_table = table[index] & PAGE_FRAME_MASK;
      if (next_table == 0 && i != 1) {
        // create new table
        uintptr_t new_table = mm_early_alloc_pages(1);
        memset((void *) new_table, 0, PAGE_SIZE);
        table[index] = new_table | PE_WRITE | PE_PRESENT;
        next_table = new_table;
      }
      table = (void *) next_table;
    }
  }

  cpu_flush_tlb();
  return addr;
}

int mm_register_mm_init_callback(void (*callback)(void *data), void *data) {
  if (callback == NULL) {
    return -EINVAL;
  }

  mm_callback_t *cb = kmalloc(sizeof(mm_callback_t));
  cb->callback = callback;
  cb->data = data;
  LIST_ENTRY_INIT(&cb->list);
  LIST_ADD(&mm_init_callbacks, cb, list);
  return 0;
}

int mm_register_vm_init_callback(void (*callback)(void *data), void *data) {
  if (callback == NULL) {
    return -EINVAL;
  }

  mm_callback_t *cb = kmalloc(sizeof(mm_callback_t));
  cb->callback = callback;
  cb->data = data;
  LIST_ENTRY_INIT(&cb->list);
  LIST_ADD(&vm_init_callbacks, cb, list);
  return 0;
}
