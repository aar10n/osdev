//
// Created by Aaron Gill-Braun on 2020-10-04.
//

#include <device/apic.h>
#include <cpu/cpu.h>
#include <cpu/idt.h>

#include <string.h>
#include <percpu.h>
#include <atomic.h>
#include <cpuid.h>
#include <panic.h>
#include <stdio.h>

// this is only ever accessed with atomic operations
static uintptr_t next_area;
static percpu_t *areas[256];
bool percpu_initialized = false;

static inline uint8_t get_cpu_id() {
  uint32_t a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) {
    panic("panic - cpuid failed");
  }
  return (b >> 24) & 0xFF;
}

//

// these functions should not call anything
// that uses spinlocks, including `kprintf`

void percpu_init_cpu() {
  uint8_t id = get_cpu_id();

  uintptr_t ptr = atomic_fetch_add(&next_area, PERCPU_SIZE);
  write_gsbase(ptr);
  write_kernel_gsbase(0);

  percpu_t *area = (void *) ptr;
  memset(area, 0, sizeof(percpu_t));
  area->id = id;
  area->self = ptr;
  area->idt = (void *)((uintptr_t) area + PAGE_SIZE);
  areas[id] = area;

  kprintf("percpu area: %p\n", area);
}

void percpu_init() {
  if (!percpu_initialized) {
    // allocate a percpu space for each logical core
    uint64_t logical_cores = boot_info->num_cores * boot_info->num_threads;
    uintptr_t paddr = boot_info->reserved_base;

    next_area = boot_info->reserved_base;
    for (int i = 0; i < logical_cores; i++) {
      void *area = (void *) paddr;
      memset(area, 0, PERCPU_SIZE);
      paddr += PERCPU_SIZE;
      boot_info->reserved_base += PERCPU_SIZE;
      boot_info->reserved_size -= PERCPU_SIZE;
    }
    percpu_initialized = true;
  }

  percpu_init_cpu();
}

