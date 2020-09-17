//
// Created by Aaron Gill-Braun on 2020-09-03.
//

#ifndef DRIVERS_ATA_DMA_H
#define DRIVERS_ATA_DMA_H

#include <stdint.h>

#define ATA_BUS_PRIMARY   0x00
#define ATA_BUS_SECONDARY 0x08

typedef struct {
  uint32_t phys_addr;
  uint16_t byte_count;
  uint16_t reserved : 15;
  uint16_t last_entry : 1;
} prdt_entry_t;

void ata_init();

#endif
