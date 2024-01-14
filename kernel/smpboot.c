//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#include <kernel/smpboot.h>
#include <kernel/percpu.h>

#include <kernel/acpi/acpi.h>
#include <kernel/hw/apic.h>

#include <kernel/mm.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)

#define SMPBOOT_START 0x1000
#define SMPDATA_START 0x2000

extern void smpboot_start();
extern void smpboot_end();

// incremented non-atomically by the bsp _only_
uint32_t system_num_cpus = 1;

int smp_boot_ap(uint32_t id, struct smp_data *smpdata) {
  // wait until AP aquires lock
  while (!smpdata->lock) cpu_pause();
  cpu_pause();
  cpu_pause();

  kprintf("smp: booting CPU#%d\n", id);

  // allocate stack and per-cpu area for ap
  struct percpu *percpu_area = percpu_alloc_area(id);
  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE));
  void *ap_stack_ptr = (void *) vmap_pages(moveref(stack_pages), 0, KERNEL_STACK_SIZE, VM_WRITE | VM_STACK, "ap stack");

  // we can do this cast "safely" because the pml4 pointer is a physical address
  // guarenteed to have been allocated from below 4GB
  smpdata->pml4_addr = (uint32_t) get_default_ap_pml4();
  smpdata->percpu_ptr = (uintptr_t) percpu_area;
  smpdata->stack_addr = (uintptr_t) ap_stack_ptr;

  // release gate
  smpdata->gate = 0;

  // wait until current AP is done
  while (!smpdata->gate) cpu_pause();

  kprintf("smp: booted CPU#%d!\n", id);
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
  kassert(sizeof(struct smp_data) <= PAGE_SIZE);
  memcpy(code_ptr, smpboot_start, smpboot_size);
  memset(data_ptr, 0, PAGE_SIZE);

  struct smp_data *smpdata = data_ptr;
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
