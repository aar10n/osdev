//
// Created by Aaron Gill-Braun on 2020-10-04.
//

#include <device/apic.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <stdio.h>
#include <string.h>
#include <percpu.h>

#include <stdatomic.h>

// this is only ever accessed with atomic operations
static uintptr_t next_area = PERCPU_BASE_VA + PAGE_SIZE;
static uint8_t initialized[256] = {};


void percpu_init() {
  kprintf("[percpu] initializing per-cpu areas\n");

  // allocate a percpu space for each core
  uint64_t logical_cores = boot_info->num_cores * boot_info->num_threads;
  kprintf("[percpu] allocating %d areas\n", logical_cores);

  uintptr_t vaddr = PERCPU_BASE_VA;
  for (int i = 0; i < logical_cores; i++) {
    // allocate pages
    page_t *pages = NULL;
    page_t *last = NULL;
    size_t remaining = PERCPU_RESERVED;
    while (remaining > 0) {
      page_t *page = mm_alloc_page(ZONE_LOW, PE_WRITE);
      if (last == NULL) {
        pages = page;
      } else {
        last->next = page;
      }
      last = page;
      remaining -= PAGE_SIZE;
    }

    void *area = vm_map_page_vaddr(vaddr, pages);
    memset(area, 0, PERCPU_RESERVED);
    vaddr += PERCPU_RESERVED;
  }

  next_area = PERCPU_BASE_VA;
  kprintf("[percpu] done!\n");
}

void percpu_init_cpu() {
  uint8_t id = apic_get_id();
  kprintf("[percpu] initializing cpu area %d\n", id);

  if (initialized[id]) {
    return;
  }

  initialized[id] = true;
  uintptr_t ptr = atomic_fetch_add(&next_area, PERCPU_RESERVED);
  kprintf("[percpu] area address: %p\n", ptr);
  write_gsbase(0);
  write_kernel_gsbase(ptr);
  swapgs();

  percpu_t *area = (void *) ptr;
  area->id = id;
  area->self = ptr;
  area->current = NULL;
  area->scheduler = NULL;
}

