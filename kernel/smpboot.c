//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#include <smpboot.h>
#include <system.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <mm.h>
#include <device/apic.h>
#include <printf.h>
#include <string.h>
#include <panic.h>

#define WAIT_COUNT 10000

extern void smpboot_start();
extern void smpboot_end();

extern gdt_desc_t gdt_desc;

int smp_boot_core(uint8_t id, smp_data_t *data) {
  apic_send_ipi(APIC_INIT, APIC_DEST_PHYSICAL, id, 0);
  apic_mdelay(10);

  uint8_t version = apic_get_version();
  if (version >= 0x10) {
    apic_send_ipi(APIC_START_UP, APIC_DEST_PHYSICAL, id, 0);
    apic_mdelay(10);
    if (data->status != AP_SUCCESS) {
      // send it again
      apic_send_ipi(APIC_START_UP, APIC_DEST_PHYSICAL, id, 0);
      apic_mdelay(10);
    }
  }

  uint64_t count = 0;
  while (data->status != AP_SUCCESS) {
    if (count > WAIT_COUNT) {
      return 0;
    }
    cpu_pause();
    count++;
  }
  return AP_SUCCESS;
}

void smp_init() {
  page_t *code_page = _alloc_pages_at(SMPBOOT_START, 1, PG_WRITE);
  page_t *data_page = _alloc_pages_at(SMPDATA_START, 1, PG_WRITE);
  void *code_ptr = _vmap_pages(code_page);
  void *data_ptr = _vmap_pages(data_page);

  memset(code_ptr, 0, PAGE_SIZE);
  memset(data_ptr, 0, PAGE_SIZE);

  // startup APs one-by-one
  size_t smpboot_size = smpboot_end - smpboot_start;
  kassert(smpboot_size < PAGE_SIZE);
  kassert(sizeof(smp_data_t) < PAGE_SIZE);
  memcpy(code_ptr, smpboot_start, smpboot_size);

  // uintptr_t pml4 = (uintptr_t) vm_create_ap_tables();
  unreachable;
  uintptr_t pml4 = (uintptr_t) NULL;
  uintptr_t stack_ptr = STACK_VA - STACK_SIZE - PAGE_SIZE;
  for (int i = 0; i < system_info->core_count; i++) {
    core_desc_t core = system_info->cores[i];
    if (core.local_apic->flags.bsp) {
      continue;
    }

    uint8_t id = core.local_apic->id;
    kprintf("[smp] booting CPU#%d\n", id);

    smp_data_t data = {
      .status = 0,
      .pml4_addr = (uint32_t) pml4,
      .stack_addr = stack_ptr,
    };

    memcpy(data_ptr, &data, sizeof(smp_data_t));
    int status = smp_boot_core(id, data_ptr);
    if (status != AP_SUCCESS) {
      kprintf("[smp] failed to boot CPU#%d\n", id);
    } else {
      kprintf("[smp] CPU#%d running!\n", id);
      stack_ptr -= STACK_SIZE + PAGE_SIZE;
    }
  }

  kprintf("[smp] done!\n");
}
