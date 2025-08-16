//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#include <kernel/smpboot.h>
#include <kernel/percpu.h>
#include <kernel/proc.h>

#include <kernel/acpi/acpi.h>
#include <kernel/hw/apic.h>
#include <kernel/hw/pit.h>

#include <kernel/mm.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <asm/bits.h>

#define ASSERT(x) kassert(x)

#define SMPBOOT_START 0x1000
#define SMPDATA_START 0x2000

extern void smpboot_start();
extern void smpboot_end();

// incremented non-atomically by the bsp _only_
uint32_t system_num_cpus = 1;

int smp_boot_ap(uint32_t id, struct smp_data *smpdata) {
  kprintf("smp: booting CPU#%d\n", id);

  // allow the specifed AP through to the gate
  smpdata->init_id = (int16_t) id;
  smpdata->ap_ack = 0;
  barrier();

  // wait until it acks and is waiting at the gate
  struct spin_delay delay = new_spin_delay(SHORT_DELAY, 1000);
  while (!smpdata->ap_ack) {
    if (!spin_delay_wait(&delay)) {
      kprintf("smp: CPU#%d did not reach the gate, aborting boot\n", id);
      return -1;
    }
  }

  // allocate stack and per-cpu area for ap
  struct percpu *percpu_area = percpu_alloc_area(id);
  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE));
  uintptr_t ap_stack_ptr = vmap_pages(moveref(stack_pages), 0, KERNEL_STACK_SIZE, VM_WRITE | VM_STACK, "ap stack");
  ASSERT(ap_stack_ptr != 0);

  // pre-allocate a main thread and idle thread
  thread_t *main_td = thread_alloc_proc0_main(ap_stack_ptr, KERNEL_STACK_SIZE);
  thread_t *idle_td = thread_alloc_idle(id);

  smpdata->pml4_addr = (uint32_t) get_default_ap_pml4();
  smpdata->percpu_ptr = (uintptr_t) percpu_area;
  smpdata->stack_addr = ap_stack_ptr + KERNEL_STACK_SIZE - KSTACK_TOP_OFF;
  smpdata->maintd_ptr = (uintptr_t) main_td;
  smpdata->idletd_ptr = (uintptr_t) idle_td;
  smpdata->space_ptr = (uintptr_t) alloc_ap_address_space();

  // release gate
  smpdata->gate = 0;
  barrier();

  // wait until the current AP finishes booting and closes gate
  delay = new_spin_delay(LONG_DELAY, MAX_RETRIES);
  while (!smpdata->gate) {
    if (!spin_delay_wait(&delay)) {
      kprintf("smp: CPU#%d failed to boot, aborting\n", id);
      // TODO: free the allocated stack and percpu area
      return -1;
    }
  }

  kprintf("smp: CPU#%d booted successfully!\n", id);
  smpdata->ap_ack = 0;
  smpdata->pml4_addr = 0;
  smpdata->percpu_ptr = 0;
  smpdata->stack_addr = 0;
  smpdata->maintd_ptr = 0;
  smpdata->idletd_ptr = 0;
  smpdata->space_ptr = 0;
  return 0;
}

void smp_init() {
  if (!is_smp_enabled || total_apic_count == 1) {
    kprintf("smp: disabled\n");
    system_num_cpus = 1;
    return;
  }

  void *code_ptr;
  page_t *code_page = alloc_pages_at(SMPBOOT_START, 1, PAGE_SIZE);
  ASSERT(code_page != NULL);
  code_ptr = (void *)vmap_pages(moveref(code_page), 0, PAGE_SIZE, VM_WRITE|VM_EXEC, "smpboot code");
  ASSERT(code_ptr != NULL);

  void *data_ptr;
  page_t *data_page = alloc_pages_at(SMPDATA_START, 1, PAGE_SIZE);
  ASSERT(data_page != NULL);
  data_ptr = (void *)vmap_pages(moveref(data_page), 0, PAGE_SIZE, VM_RDWR|VM_NOCACHE, "smpboot data");
  ASSERT(data_ptr != NULL);

  uintptr_t eip = virt_to_phys(code_ptr);

  size_t smpboot_size = smpboot_end - smpboot_start;
  kassert(smpboot_size <= PAGE_SIZE);
  kassert(sizeof(struct smp_data) <= PAGE_SIZE);
  memcpy(code_ptr, smpboot_start, smpboot_size);
  memset(data_ptr, 0, PAGE_SIZE);

  struct smp_data *smpdata = data_ptr;
  smpdata->init_id = -1;
  smpdata->gate = 1;
  smpdata->ap_ack = 0;

  // issue INIT-SIPI-SIPI Sequence to start-up all APs
  kprintf("smp: issuing sequence to start all APs...\n");

  // INIT
  apic_write_icr(APIC_DM_INIT | APIC_LVL_ASSERT | APIC_DS_ALLBUT, 0);
  pit_mdelay(10); // required 10ms delay after INIT
  // SIPI
  apic_write_icr(APIC_DM_STARTUP | APIC_LVL_ASSERT | APIC_DS_ALLBUT | (eip >> 12), 0);
  pit_mdelay(1); // min 200us delay between SIPI messages
  // SIPI
  apic_write_icr(APIC_DM_STARTUP | APIC_LVL_ASSERT | APIC_DS_ALLBUT | (eip >> 12), 0);

  // give the APs 100ms to reach the smpboot lock
  pit_mdelay(100);

  // now try to initialize the APs one by one
  for (int i = 1; i < total_apic_count; i++) {
    uint8_t apic_id = apic_id_map[i];
    if (!(smpdata->ack_bitmap[i / 32] & (1 << (i % 32)))) {
      kprintf("smp: CPU#%d did not respond, skipping...\n", apic_id);
      continue;
    }

    // AP responded to the start sequence, try to boot it
    if (smp_boot_ap(apic_id, smpdata) < 0) {
      // failed to boot AP
      continue;
    }
    system_num_cpus++;
  }

  vmap_free((uintptr_t) code_ptr, PAGE_SIZE);
  vmap_free((uintptr_t) data_ptr, PAGE_SIZE);
  kprintf("smp: total cpus = %d\n", system_num_cpus);
  kprintf("smp: done!\n");
}
