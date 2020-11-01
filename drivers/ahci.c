//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <drivers/ahci.h>
#include <device/ioapic.h>
#include <cpu/idt.h>
#include <bus/pci.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/heap.h>
#include <panic.h>
#include <stdio.h>
#include <string.h>
#include <vectors.h>

ahci_dev_t *ahci;

typedef enum {
  DEVICE_TO_HOST, // read
  HOST_TO_DEVICE, // write
} direction_t;

//

void interrupt_handler() {
  kprintf("[ahci] interrupt!\n");
}

int get_port_type(hba_port_t *port) {
  uint32_t status = port->sata_status;
  uint32_t ipm = (status >> 8) & 0xF;
  uint32_t det = status & 0xF;

  if (det != HBA_PORT_DET_PRESENT) {
    return AHCI_DEV_NULL;
  }
  if (ipm != HBA_PORT_IPM_ACTIVE) {
    return AHCI_DEV_NULL;
  }

  switch (port->signature) {
    case SATA_SIG_ATA:
      return AHCI_DEV_SATA;
    case SATA_SIG_ATAPI:
      return AHCI_DEV_SATAPI;
    case SATA_SIG_SEMB:
      return AHCI_DEV_SEMB;
    case SATA_SIG_PM:
      return AHCI_DEV_PM;
    default:
      return AHCI_DEV_NULL;
  }
}

void port_command_start(hba_port_t *port) {
  // wait until CR is cleared
  while (port->command & HBA_PxCMD_CR) {
    cpu_pause();
  }

  // set the FRE and ST bits
  port->command |= HBA_PxCMD_FRE;
  port->command |= HBA_PxCMD_ST;
}

void port_command_stop(hba_port_t *port) {
  // clear the ST and FRE bits
  port->command &= ~HBA_PxCMD_ST;
  port->command &= ~HBA_PxCMD_FRE;

  // wait until FR and CR are cleared
  uint32_t mask = HBA_PxCMD_FR | HBA_PxCMD_CR;
  while (port->command & mask) {
    cpu_pause();
  }
}

//

ahci_port_t *port_init(hba_reg_mem_t *mem, int port_num) {
  hba_port_t *port = &(mem->ports[port_num]);

  int type = get_port_type(port);
  ahci_port_t *sata_port = kmalloc(sizeof(ahci_port_t));
  sata_port->num = port_num;
  sata_port->type = type;
  sata_port->port = port;
  sata_port->fis = NULL;
  sata_port->slots = NULL;
  if (type != AHCI_DEV_SATA) {
    return sata_port;
  }

  kprintf("[sata] initializing port %d\n", port_num);

  port_command_stop(port);

  // command list
  page_t *cmd_list_page = alloc_page(PE_WRITE | PE_CACHE_DISABLE);
  void *cmd_list = vm_map_page(cmd_list_page);
  port->cmd_list_base = (uintptr_t) cmd_list_page->frame;
  memset(cmd_list, 0, PAGE_SIZE);

  // fis structure
  page_t *fis_page = alloc_page(PE_WRITE | PE_CACHE_DISABLE);
  void *fis = vm_map_page(fis_page);
  port->fis_base = fis_page->frame;
  memset(fis, 0, PAGE_SIZE);

  sata_port->fis = fis;

  port->sata_error = 1;
  port->int_status = 0;
  port->int_enable = 0;

  // command tables
  size_t num_slots = mem->host_cap.num_cmd_slots;
  kprintf("[sata] port %d: %d slots\n", port_num, num_slots);
  ahci_slot_t **slots = kmalloc(sizeof(ahci_slot_t *) * num_slots);
  hba_cmd_header_t *headers = (void *) cmd_list;
  for (int i = 0; i < num_slots; i++) {
    page_t *table_page = alloc_page(PE_WRITE | PE_CACHE_DISABLE);
    void *table = vm_map_page(table_page);
    memset(table, 0, PAGE_SIZE);

    hba_cmd_header_t *header = &(headers[i]);
    header->prdt_length = 8;
    header->cmd_table_base = table_page->frame;

    ahci_slot_t *slot = kmalloc(sizeof(ahci_slot_t));
    slot->num = i;
    slot->header = headers;
    slot->table = table;
    slot->table_length = PAGES_TO_SIZE(2);

    slots[i] = slot;
  }

  sata_port->slots = slots;
  port_command_start(port);

  port->int_status = 0;
  port->int_enable = 0xFFFFFFFF;
  return sata_port;
}

void sata_probe_ports(hba_reg_mem_t *abar) {
  for (int i = 0; i < abar->host_cap.num_ports; i++) {
    if (!(abar->port_implemented & (1 << i))) {
      continue;
    }

    hba_port_t *port = &(abar->ports[i]);
    int type = get_port_type(port);
    if (type == AHCI_DEV_SATA) {
      kprintf("[ahci] found SATA drive on port %d\n", i);
    } else if (type == AHCI_DEV_SATAPI) {
      kprintf("[ahci] found SATAPI drive on port %d\n", i);
    } else if (type == AHCI_DEV_SEMB) {
      kprintf("[ahci] found SEMB drive on port %d\n", i);
    } else if (type == AHCI_DEV_PM) {
      kprintf("[ahci] found PM drive on port %d\n", i);
    }
  }
}

ssize_t transfer_dma(direction_t dir, ahci_port_t *ahci_port, uintptr_t lba, size_t count, uintptr_t buf) {
  kassert(count <= 128);
  ahci_slot_t *ahci_slot = ahci_port->slots[0]; // TODO: pick a better slot
  hba_port_t *port = ahci_port->port;

  hba_cmd_header_t *cmd = ahci_slot->header;
  cmd->fis_length = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  cmd->write = dir == HOST_TO_DEVICE;
  cmd->prefetch = 1;
  cmd->clear_bsy_ok = 1;
  cmd->prdt_length = ((count - 1) >> 4) + 1;

  hba_cmd_table_t *table = ahci_slot->table;
  kprintf("cmd->prdt_length: %d\n", cmd->prdt_length);
  memset(table->prdt, 0, cmd->prdt_length * sizeof(hba_prdt_entry_t));

  // create as many 8k entries as we can
  for (int i = 0; i < cmd->prdt_length - 1; i++) {
    uint32_t byte_count = (16 * 512) - 1;
    kprintf("[ahci] byte count: %d\n", byte_count);
    table->prdt[i].data_base = buf;
    table->prdt[i].byte_count = byte_count;
    table->prdt[i].ioc = 0;
    buf += byte_count;
    count -= 16;
  }

  // make a final entry for whatever is left
  int last = cmd->prdt_length - 1;
  table->prdt[last].data_base = buf;
  table->prdt[last].byte_count = count * 512;
  table->prdt[last].ioc = 0;

  // setup command
  fis_reg_h2d_t *fis = (void *) &(table->cmd_fis);
  fis->fis_type = FIS_TYPE_REG_H2D;
  fis->cmd_ctrl = 1; // command
  fis->command = dir == HOST_TO_DEVICE ?
    ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;

  fis->lba0 = lba;
  fis->lba1 = lba >> 8;
  fis->lba2 = lba >> 16;
  fis->device = 1 << 6; // lba mode

  fis->lba3 = lba >> 24;
  fis->lba4 = lba >> 32;
  fis->lba5 = lba >> 40;

  fis->count_low = count;
  fis->count_high = count >> 8;

  // wait for any pending operations to complete
  uint64_t timeout = 1000000;
  while ((port->task_file_data & (ATA_DEV_BUSY | ATA_DEV_DRQ))) {
    if (timeout == 0) {
      kprintf("[ahci] drive is hung\n");
      return -1;
    }
    timeout--;
  }

  // issue command
  port->command_issue = 1 << ahci_slot->num;

  timeout = 1000000;
  while (true) {
    // wait for completion
    if ((port->command_issue & (1 << ahci_slot->num)) == 0) {
      break;
    } else if (port->int_status & HBA_PxIS_TFES) {
      // task file error
      kprintf("[ahci] task file error\n");
      return -1;
    } else if (timeout == 0) {
      kprintf("[ahci] transfer timed out\n");
      return -1;
    }
    timeout--;
  }

  // check for error one more time
  if (port->int_status & HBA_PxIS_TFES) {
    kprintf("[ahci] error reading disk\n");
    return -1;
  }
  return cmd->prdb_transf_cnt;
}

//

void ahci_init() {
  pci_device_t *pci = pci_locate_device(
    PCI_STORAGE_CONTROLLER,
    PCI_SERIAL_ATA_CONTROLLER
  );

  kassert(pci->bar_count >= 6);
  pci_bar_t bar5 = pci->bars[5];

  size_t mmio_size = bar5.mem.addr_end - bar5.mem.addr_start;
  void *ahci_base = vm_map_addr(bar5.mem.addr_start, mmio_size, PE_WRITE);

  hba_reg_mem_t *hba_mem = ahci_base;
  sata_probe_ports(hba_mem);

  int num_ports = hba_mem->host_cap.num_ports;
  ahci_port_t **ports = kmalloc(sizeof(ahci_port_t *) * num_ports);
  for (int i = 0; i < hba_mem->host_cap.num_ports; i++) {
    ports[i] = port_init(hba_mem, i);
  }

  ahci_dev_t *dev = kmalloc(sizeof(ahci_dev_t));
  dev->id = 0;
  dev->mem = hba_mem;
  dev->ports = ports;
  dev->pci = pci;
  ahci = dev;

  ioapic_set_irq(0, 11, VECTOR_SATA_IRQ);
  idt_hook(VECTOR_SATA_IRQ, interrupt_handler);
}

ssize_t ahci_read(int port_num, uintptr_t lba, uint32_t count, uintptr_t buf) {
  ahci_port_t *port = ahci->ports[port_num];
  return transfer_dma(DEVICE_TO_HOST, port, lba, count, buf);
}

ssize_t ahci_write(int port_num, uintptr_t lba, uint32_t count, uintptr_t buf) {
  ahci_port_t *port = ahci->ports[port_num];
  return transfer_dma(HOST_TO_DEVICE, port, lba, count, buf);
}
