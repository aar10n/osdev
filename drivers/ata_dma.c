//
// Created by Aaron Gill-Braun on 2020-09-03.
//

#include <drivers/ata_dma.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>
#include <stdio.h>

page_t *prdt_page = NULL;
uint64_t *prdt = NULL;

page_t *data_buffer = NULL;

// byte offset		function
// (Primary ATA bus)
// 0x0			Command (byte)
// 0x2			Status (byte)
// 0x4-0x7	        PRDT Address (uint32_t)
// (Secondary ATA bus)
// 0x8			Command (byte)
// 0xA			Status (byte)
// 0xC-0xF		PRDT Address (uint32_t)

void ata_init() {
  if (prdt != NULL) {
    kprintf("[ata] already initialized\n");
    return;
  }

  page_t *page = alloc_page(ZONE_DMA);
  page->virt_addr = page->phys_addr;
  map_page(page);

  prdt_page = page;
  prdt = (uint64_t *) page->phys_addr;
}
