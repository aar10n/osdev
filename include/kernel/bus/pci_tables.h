//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef KERNEL_BUS_PCI_TABLES_H
#define KERNEL_BUS_PCI_TABLES_H

#include <kernel/base.h>

typedef struct pci_desc {
  uint8_t class_code;
  uint8_t subclass_code;
  uint8_t prog_if;
  const char *desc;
} pci_desc_t;

const char *pci_get_device_desc(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if);

#endif
