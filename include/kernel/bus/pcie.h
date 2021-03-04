//
// Created by Aaron Gill-Braun on 2021-02-18.
//

#ifndef KERNEL_BUS_PCIE_H
#define KERNEL_BUS_PCIE_H

#include <base.h>
#include <bus/pci.h>

#define PCIE_MMIO_SIZE 268435456 // 256 MiB

typedef struct {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t command;
  uint16_t status;
  uint32_t rev_id : 8;
  uint32_t class_code : 24;
  uint8_t cache_line_sz;
  uint8_t latency_timer;
  uint8_t header_type;
  uint8_t bist;
} pcie_header_t;

void pcie_init();
void pcie_discover();

#endif
