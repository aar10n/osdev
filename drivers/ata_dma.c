//
// Created by Aaron Gill-Braun on 2020-09-03.
//

#include <drivers/ata_dma.h>
#include <kernel/bus/pci.h>
#include <kernel/cpu/asm.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>
#include <stdio.h>

#define OFFSET_COMMAND 0x00
#define OFFSET_STATUS  0x02
#define OFFSET_PRDT    0x04

page_t *prdt_page = NULL;
uint64_t *prdt = NULL;

page_t *data_buffer = NULL;

uint16_t io_port = 0;
uint16_t io_port_end = 0;

// byte offset		function
// (Primary ATA bus)
// 0x0			Command (byte)
// 0x2			Status (byte)
// 0x4-0x7	        PRDT Address (uint32_t)
// (Secondary ATA bus)
// 0x8			Command (byte)
// 0xA			Status (byte)
// 0xC-0xF		PRDT Address (uint32_t)

static inline uint16_t get_port(uint8_t bus, uint8_t offset) {
  return io_port + bus + offset;
}

//

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

  pci_device_t *device = pci_locate_device(PCI_STORAGE_CONTROLLER, PCI_IDE_CONTROLLER);
  pci_print_debug_device(device);
  pci_bar_t bar4 = device->bars[4];

  io_port = bar4.io.addr_start;
  io_port_end = bar4.io.addr_end;

  outb(get_port(ATA_BUS_PRIMARY, OFFSET_COMMAND), 0b1);
}

// Debugging

void ata_print_debug_status(uint8_t bus) {

}
