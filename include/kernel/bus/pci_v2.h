//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef KERNEL_BUS_PCI_V2_H
#define KERNEL_BUS_PCI_V2_H

#include <kernel/base.h>
#include <kernel/queue.h>

typedef struct pci_bar {
  uint8_t num : 3;      // bar number
  uint8_t kind : 1;     // bar kind (0 = mem, 1 = io)
  uint8_t type : 2;     // memory type (0 = 32-bit, 1 = 64-bit, 2 = 64-bit 32-bit)
  uint8_t prefetch : 1; // prefetchable (only for mem)
  uint8_t : 1;          // reserved
  uint64_t phys_addr;   // base physical address
  uint64_t virt_addr;   // virtual address
  uint64_t size;        // memory size
  SLIST_ENTRY(struct pci_bar) next; // next bar
} pci_bar_t;

typedef struct pci_cap {
  uint8_t id;           // capability id
  uintptr_t offset;     // offset to cap
  SLIST_ENTRY(struct pci_cap) next; // next cap
} pci_cap_t;

typedef struct pci_device {
  uint16_t device_id;
  uint16_t vendor_id;

  uint8_t bus;
  uint8_t device : 5;
  uint8_t function : 3;

  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t int_line;
  uint8_t int_pin;

  uint16_t subsystem;
  uint16_t subsystem_vendor;
} pci_device_t;

void register_pci_segment_group(uint16_t number, uint8_t start_bus, uint8_t end_bus, uintptr_t address);

#endif
