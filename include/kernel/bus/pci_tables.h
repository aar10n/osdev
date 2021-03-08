//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef KERNEL_BUS_PCI_TABLES_H
#define KERNEL_BUS_PCI_TABLES_H

#include <base.h>

typedef struct pci_subclass {
  uint8_t class_code;
  uint8_t subclass_code;
  const char *desc;
} pci_subclass_t;

const char *pci_get_class_desc(uint8_t class_code);
const char *pci_get_subclass_desc(uint8_t class_code, uint8_t subclass_code);



#endif
