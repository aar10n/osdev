//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#include <kernel/smpboot.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/per_cpu.h>
#include <kernel/device/apic.h>

#include <kernel/mm.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)

#define SMPBOOT_START 0x1000
#define SMPDATA_START 0x2000

extern void smpboot_start();
extern void smpboot_end();

uint32_t system_num_cpus = 1;

int smp_boot_ap(uint16_t id, smp_data_t *smpdata) {
  // wait until AP aquires lock
  while (!smpdata->lock) cpu_pause();
  cpu_pause();
  cpu_pause();

  uint8_t apic_id = smpdata->current_id;
  kprintf("smp: booting CPU#%d\n", apic_id);

  // allocate stack and per-cpu area for ap
  per_cpu_t *percpu = percpu_alloc_area(id, apic_id);

  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE));
  vm_mapping_t *stack_vm = vmap_pages(stack_pages, 0, KERNEL_STACK_SIZE, VM_WRITE | VM_STACK, "ap stack");
  void *ap_stack_ptr = (void *) stack_vm->address;

  // setup ap page tables
  uintptr_t ap_pml4 = make_ap_page_tables();
  smpdata->pml4_addr = (uint32_t) ap_pml4;
  smpdata->percpu_ptr = (uintptr_t) percpu;
  smpdata->stack_addr = (uintptr_t) ap_stack_ptr;

  // release gate
  smpdata->gate = 0;

  // wait until current AP is done
  while (!smpdata->gate) cpu_pause();

  kprintf("smp: booted CPU#%d!\n", apic_id);
  smpdata->pml4_addr = 0;
  smpdata->percpu_ptr = 0;
  smpdata->stack_addr = 0;
  return 0;
}

void smp_init() {
  if (!is_smp_enabled) {
    kprintf("smp: disabled\n");
    system_num_cpus = 1;
    return;
  }

  void *code_ptr = vmalloc_at_phys(SMPBOOT_START, PAGE_SIZE, VM_WRITE | VM_EXEC);
  void *data_ptr = vmalloc_at_phys(SMPDATA_START, PAGE_SIZE, VM_WRITE | VM_NOCACHE);
  uintptr_t eip = vm_virt_to_phys((uintptr_t) code_ptr);

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
    uint16_t id = i + 1;
    smp_boot_ap(id, smpdata);
    system_num_cpus++;
  }

  vfree(code_ptr);
  vfree(data_ptr);
  kprintf("smp: total cpus = %d\n", system_num_cpus);
  kprintf("smp: done!\n");
}
