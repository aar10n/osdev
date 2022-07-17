//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#include <smpboot.h>

#include <acpi/acpi.h>
#include <device/apic.h>

#include <mm.h>
#include <printf.h>
#include <panic.h>
#include <string.h>

#define SMPBOOT_START 0x1000
#define SMPDATA_START 0x2000
#define WAIT_COUNT 10000

extern void smpboot_start();
extern void smpboot_end();


int smp_boot_ap(smp_data_t *smpdata) {
  // wait until AP aquires lock
  while (!smpdata->lock) cpu_pause();
  cpu_pause();
  cpu_pause();

  uint8_t apic_id = smpdata->current_id;
  kprintf("smp: booting CPU#%d\n", apic_id);

  // allocate stack and per-cpu area for ap
  uintptr_t ap_pml4 = make_ap_page_tables();
  page_t *percpu_pages = _alloc_pages(SIZE_TO_PAGES(PER_CPU_SIZE), PG_WRITE);
  void *ap_percpu_ptr = _vmap_pages(percpu_pages);
  page_t *stack_pages = _alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE), PG_WRITE);
  void *ap_stack_ptr = _vmap_stack_pages(stack_pages);

  memset(ap_percpu_ptr, 0, PER_CPU_SIZE);
  ((per_cpu_t *)(ap_percpu_ptr))->self = (uintptr_t) ap_percpu_ptr;
  ((per_cpu_t *)(ap_percpu_ptr))->id = apic_id;

  smpdata->pml4_addr = (uint32_t) ap_pml4;
  smpdata->percpu_ptr = (uintptr_t) ap_percpu_ptr;
  smpdata->stack_addr = (uintptr_t) ap_stack_ptr;

  kprintf("smp: releasing gate\n");
  // release gate
  smpdata->gate = 0;

  // wait until current AP is done
  while (!smpdata->gate) cpu_pause();

  kprintf("smp: booted CPU#%d!\n", apic_id);
  // while (true) cpu_pause();
  smpdata->pml4_addr = 0;
  smpdata->percpu_ptr = 0;
  smpdata->stack_addr = 0;
  return 0;
}

void smp_init() {
  page_t *code_pages = _alloc_pages_at(SMPBOOT_START, 1, PG_WRITE | PG_EXEC | PG_FORCE);
  page_t *data_pages = _alloc_pages_at(SMPDATA_START, 1, PG_NOCACHE | PG_WRITE | PG_FORCE);
  uintptr_t eip = PAGE_PHYS_ADDR(code_pages);
  void *code_ptr = _vmap_pages(code_pages);
  void *data_ptr = _vmap_pages(data_pages);
  kassert(code_ptr != NULL);
  kassert(data_ptr != NULL);

  kprintf("smp: code pointer = %018p\n", code_ptr);
  kprintf("smp: data pointer = %018p\n", data_ptr);

  size_t smpboot_size = smpboot_end - smpboot_start;
  kassert(smpboot_size <= PAGE_SIZE);
  kassert(sizeof(smp_data_t) <= PAGE_SIZE);
  memcpy(code_ptr, smpboot_start, smpboot_size);
  memset(data_ptr, 0, PAGE_SIZE);

  smp_data_t *smpdata = data_ptr;
  smpdata->lock = 0;
  smpdata->gate = 1;

  // issue INIT-SIPI-SIPI Sequence to start-up all APs
  // INIT
  apic_write_icr(APIC_DM_INIT | APIC_LVL_ASSERT | APIC_DS_ALLBUT, 0);
  apic_mdelay(10);
  // SIPI
  apic_write_icr(APIC_DM_STARTUP | APIC_LVL_ASSERT | APIC_DS_ALLBUT | (eip >> 12), 0);
  apic_udelay(200);
  // SIPI
  apic_write_icr(APIC_DM_STARTUP | APIC_LVL_ASSERT | APIC_DS_ALLBUT | (eip >> 12), 0);
  apic_udelay(200);

  uint32_t timeout_us = 100;
  while (timeout_us > 0 && smpdata->count < total_apic_count - 1) {
    cpu_pause();
    apic_udelay(1);
    timeout_us--;
  }

  for (int i = 0; i < smpdata->count; i++) {
    smp_boot_ap(smpdata);
  }

  kprintf("smp: done!\n");
  while (true) cpu_pause();

  _free_pages(code_pages);
  _free_pages(data_pages);
  kprintf("[smp] done!\n");
}
