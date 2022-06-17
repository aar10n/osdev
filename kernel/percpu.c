//
// Created by Aaron Gill-Braun on 2020-10-04.
//

#include <device/apic.h>
#include <cpu/cpu.h>
#include <cpu/idt.h>

#include <string.h>
#include <percpu.h>
#include <atomic.h>
#include <panic.h>
#include <printf.h>


uint32_t bsp_id = 0;

// this is only ever accessed with atomic operations
static uintptr_t next_area;
static percpu_t *areas[256];
bool percpu_initialized = false;

//

// these functions should not call anything
// that uses spinlocks, including `kprintf`

void percpu_init_cpu() {
  uint32_t id = cpu_get_id();

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
  if (cpu_get_is_bsp()) {
    // place the first percpu area immediately after the kernel
    uintptr_t kernel_end = boot_info_v2->kernel_virt_addr + boot_info_v2->kernel_size;
    boot_info_v2->kernel_size += PERCPU_SIZE;
    percpu_t *area = (void *) kernel_end;

    uint32_t id = cpu_get_id();
    memset(area, 0, PERCPU_SIZE);
    area->id = id;
    area->self = (uintptr_t) area;

    bsp_id = id;
    write_gsbase((uintptr_t) area);
    write_kernel_gsbase(0);
  } else {
    // initialize the AP percpu area
  }
}

