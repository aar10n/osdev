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
#include <dev.h>

#include <asm/bitmap.h>

typedef enum {
  DEVICE_TO_HOST, // read
  HOST_TO_DEVICE, // write
} direction_t;

// support only one ahci controller for now
ahci_dev_t *ahci_controller;


void interrupt_handler() {
  hba_reg_mem_t *mem = ahci_controller->mem;
  // find ports that needs servicing
  while (mem->int_status != 0) {
    int num = __bsf32(mem->int_status);

    ahci_port_t *ahci_port = ahci_controller->ports[num];
    hba_port_t *port = ahci_port->port;

    uint32_t int_status = port->int_status;
    kprintf("[ahci] interrupt on port %d (0x%X)\n", num, int_status);
    port->int_status = 0xFFFFFFFF; // clear status

    mem->int_status ^= 1 << num;
  }
}

bool is_ahci_drive(device_t *device) {
  return device && device->type == STORAGE_DEVICE &&
    device->subtype == AHCI_STORAGE_DEVICE;
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

ahci_port_t *port_init(ahci_dev_t *controller, int port_num) {
  hba_reg_mem_t *mem = controller->mem;
  hba_port_t *port = &(mem->ports[port_num]);

  int type = get_port_type(port);
  ahci_port_t *ahci_port = kmalloc(sizeof(ahci_port_t));
  ahci_port->num = port_num;
  ahci_port->type = type;
  ahci_port->port = port;
  ahci_port->fis = NULL;
  ahci_port->slots = NULL;
  if (type != AHCI_DEV_SATA) {
    return ahci_port;
  }

  kprintf("[ahci] initializing port %d\n", port_num);

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
  kprintf("[ahci] fis base: %p\n", fis_page->frame);
  memset(fis, 0, PAGE_SIZE);

  ahci_port->fis = fis;

  port->sata_error = 1;
  port->int_status = 0xFFFFFFFF;
  port->int_enable = 0;

  // command tables
  size_t num_slots = mem->host_cap.num_cmd_slots;
  kprintf("[ahci] port %d: %d slots\n", port_num, num_slots);
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

  ahci_port->slots = slots;
  port_command_start(port);

  port->int_status = 0;
  return ahci_port;
}

void ahci_discover(ahci_dev_t *controller) {
  kprintf("[ahci] discovering devices...\n");
  hba_reg_mem_t *hba_mem = controller->mem;

  int num_ports = hba_mem->host_cap.num_ports;
  ahci_port_t **ports = kmalloc(sizeof(ahci_port_t *) * num_ports);
  for (int i = 0; i < num_ports; i++) {
    int type = get_port_type(&(hba_mem->ports[i]));
    if (type == AHCI_DEV_SATA) {
      kprintf("[ahci] found SATA drive on port %d\n", i);

      ahci_port_t *port = port_init(controller, i);
      ports[i] = port;
      // register sata drive
      dev_t id = device_register(
        STORAGE_DEVICE, AHCI_STORAGE_DEVICE, controller->id,
        "SATA Drive", NULL, port
      );
      port->id = id;
    } else {
      ports[i] = NULL;
    }
  }

  controller->ports = ports;
}

ssize_t transfer_dma(direction_t dir, ahci_port_t *ahci_port, uintptr_t lba, size_t count, uintptr_t buf) {
  kassert(count <= 128);
  ahci_slot_t *ahci_slot = ahci_port->slots[0]; // TODO: pick a better slot
  hba_port_t *port = ahci_port->port;
  // enable all interrupts
  port->int_enable = 0xFFFFFFFF;

  hba_cmd_header_t *cmd = ahci_slot->header;
  cmd->fis_length = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  cmd->write = dir == HOST_TO_DEVICE;
  cmd->prefetch = 1;
  cmd->clear_bsy_ok = 1;
  cmd->prdt_length = ((count - 1) >> 4) + 1;

  size_t max_prdt_entries = ((ahci_slot->table_length - sizeof(hba_cmd_table_t)) /
    sizeof(hba_prdt_entry_t)) + 1;
  kprintf("max prdt entries: %d\n", max_prdt_entries);
  hba_cmd_table_t *table = ahci_slot->table;
  memset(table->prdt, 0, cmd->prdt_length * sizeof(hba_prdt_entry_t));

  // setup command FIS
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

  // create the prdt entries
  int index = 0;
  while (count > 0) {
    uint8_t sec_count = min(count, 16);
    uint32_t byte_count = sec_count * 512;

    table->prdt[index].data_base = buf;
    table->prdt[index].ioc = 0;
    if (count - sec_count == 0) {
      // last entry (first bit in byte_count must be 1)
      table->prdt[index].byte_count = byte_count;
    } else {
      table->prdt[index].byte_count = byte_count - 1;
    }

    index++;
    count -= sec_count;
    buf += byte_count;
  }

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

  kprintf("[ahci] transfer count: %d\n", cmd->prdb_transf_cnt);
  return cmd->prdb_transf_cnt;
}

//

void ahci_init() {
  kprintf("[ahci] initializing\n");
  pci_device_t *pci = pci_locate_device(
    PCI_STORAGE_CONTROLLER,
    PCI_SERIAL_ATA_CONTROLLER
  );
  if (pci == NULL) {
    kprintf("[ahci] no ahci controller found\n");
    return;
  }

  kassert(pci->bar_count >= 6);
  pci_bar_t bar5 = pci->bars[5];

  size_t mmio_size = bar5.mem.addr_end - bar5.mem.addr_start;
  void *ahci_base = vm_map_addr(bar5.mem.addr_start, mmio_size, PE_WRITE);

  hba_reg_mem_t *hba_mem = ahci_base;

  ahci_dev_t *controller = kmalloc(sizeof(ahci_dev_t));
  // register the ahci controller
  dev_t id = device_register(
    STORAGE_CONTROLLER, AHCI_STORAGE_CONTROLLER, 0,
    "SATA Controller", pci, controller
  );
  controller->id = id;
  controller->mem = hba_mem;
  controller->pci = pci;

  // discover sata drives
  ahci_discover(controller);

  // configure the global host control
  hba_mem->global_host_ctrl |= HBA_CTRL_AHCI_ENABLE;
  hba_mem->global_host_ctrl |= HBA_CTRL_INT_ENABLE;

  ahci_controller = controller;
  ioapic_set_irq(0, pci->interrupt_line, VECTOR_AHCI_IRQ);
  idt_hook(VECTOR_AHCI_IRQ, interrupt_handler);
  kprintf("[ahci] done!\n");
}

ssize_t ahci_read(dev_t dev, uintptr_t lba, uint32_t count, uintptr_t buf) {
  device_t *device = device_get(dev);
  if (!is_ahci_drive(device)) {
    kprintf("[ahci] invalid device\n");
    return -1;
  }

  ahci_port_t *port = device->data;
  return transfer_dma(DEVICE_TO_HOST, port, lba, count, buf);
}

ssize_t ahci_write(dev_t dev, uintptr_t lba, uint32_t count, uintptr_t buf) {
  device_t *device = device_get(dev);
  if (!is_ahci_drive(device)) {
    kprintf("[ahci] invalid device\n");
    return -1;
  }

  ahci_port_t *port = device->data;
  return transfer_dma(HOST_TO_DEVICE, port, lba, count, buf);
}
