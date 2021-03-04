//
// Created by Aaron Gill-Braun on 2021-02-18.
//

#include <bus/pcie.h>
#include <mm/vm.h>
#include <stdio.h>
#include <panic.h>
#include <system.h>

#define PCIE_BASE (system_info->pcie->virt_addr)
#define pcie_addr(bus, device, function) \
  ((void *)(PCIE_BASE | ((bus) << 20) | ((device) << 15) | ((function) << 12)))

void pcie_init() {
  pcie_desc_t *pcie = system_info->pcie;
  uintptr_t addr = MMIO_BASE_VA;
  if (!vm_find_free_area(ABOVE, &addr, PCIE_MMIO_SIZE)) {
    panic("[pcie] no free address space");
  }
  pcie->virt_addr = addr;
  vm_map_vaddr(pcie->virt_addr, pcie->phys_addr, PCIE_MMIO_SIZE, PE_WRITE);

  kprintf("[pcie] physical address: %p\n", pcie->phys_addr);
  kprintf("[pcie] virtual address: %p\n", pcie->virt_addr);
}

void pcie_discover() {
  kprintf("[pcie] discovering devices...\n");
  int bus = 0;
  for (int device = 0; device < 32; device++) {
    for (int function = 0; function < 8; function++) {
      pcie_header_t *header = pcie_addr(bus, device, function);
      kprintf("vendor id: %#X\n", header->vendor_id);
      if (header->vendor_id == 0xFFFF) {
        return;
      }
    }
  }
}
