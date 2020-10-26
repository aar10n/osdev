//
// Created by Aaron Gill-Braun on 2020-10-04.
//

#include <device/apic.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <stdio.h>
#include <string.h>
#include <percpu.h>
#include <atomic.h>
#include <cpuid.h>
#include <panic.h>

// this is only ever accessed with atomic operations
static uintptr_t next_area;
percpu_t *areas[256];

static inline uint8_t get_cpu_id() {
  uint32_t a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) {
    panic("panic - cpuid failed");
  }
  return (b >> 24) & 0xFF;
}

//

void percpu_init() {
  kprintf("[percpu] initializing per-cpu areas\n");

  // allocate a percpu space for each logical core
  uint64_t logical_cores = boot_info->num_cores * boot_info->num_threads;
  kprintf("[percpu] allocating %d areas\n", logical_cores);

  uintptr_t paddr = boot_info->reserved_base;
  for (int i = 0; i < logical_cores; i++) {
    void *area = (void *) paddr;
    memset(area, 0, PERCPU_SIZE);
    paddr += PERCPU_SIZE;
  }

  next_area = boot_info->reserved_base;
  boot_info->reserved_base += PERCPU_SIZE;
  boot_info->reserved_size -= PERCPU_SIZE;
  kprintf("[percpu] done!\n");
}

void percpu_init_cpu() {
  uint8_t id = get_cpu_id();
  kprintf("[percpu] initializing cpu area %d\n", id);

  uintptr_t ptr = atomic_fetch_add(&next_area, PERCPU_SIZE);
  kprintf("[percpu] area address: %p\n", ptr);
  write_gsbase(ptr);
  write_kernel_gsbase(0);

  percpu_t *area = (void *) ptr;
  memset(area, 0, sizeof(percpu_t));
  area->id = id;
  area->self = ptr;
  areas[id] = area;
}

