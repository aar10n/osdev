//
// Created by Aaron Gill-Braun on 2020-09-01.
//

#include <kernel/bus/pci.h>
#include <kernel/bus/pci_tables.h>
#include <kernel/cpu/asm.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

//

#define config_address(bus, device, function) \
  ((uint32_t) (((bus) & 0xFF) << 16) |        \
   (((device) & 0x1F) << 11) |                \
   (((function) & 0x07) << 8) |               \
   0x80000000)

#define device_config_address(device) \
  config_address((device)->bus, (device)->device, (device)->function)

#define register_address(addr, offset) \
  ((uint32_t) (addr) | ((offset) & 0xFF))

#define is_device_valid(device) \
  ((device)->vendor_id != 0xFFFF)


struct locate_context {
  uint8_t device_class;
  uint8_t device_subclass;
  pci_device_t **result;
};

//

uint32_t pci_read(uint32_t addr, uint8_t offset) {
  outdw(PCI_CONFIG_ADDR, register_address(addr, offset));
  return indw(PCI_CONFIG_DATA);
}

void pci_write(uint32_t addr, uint8_t offset, uint32_t value) {
  outdw(PCI_CONFIG_ADDR, register_address(addr, offset));
  outdw(PCI_CONFIG_DATA, value);
}

//

void pci_read_device_info(pci_device_t *device) {
  uint32_t addr = config_address(device->bus, device->device, device->function);
  uint32_t reg0 = pci_read(addr, 0x00);
  uint32_t reg2 = pci_read(addr, 0x08);
  uint32_t reg3 = pci_read(addr, 0x0C);
  uint32_t reg15 = pci_read(addr, 0x3C);

  device->vendor_id = reg0 & 0xFFFF; // 1st double-word
  device->device_id = (reg0 >> 16) & 0xFFFF; // 2nd double-word

  // exit early if device is invalid
  if (!is_device_valid(device)) {
    return;
  }

  device->prog_if = (reg2 >> 8) & 0xFF; // 1st word
  device->subclass_code = (reg2 >> 16) & 0xFF; // 2nd word
  device->class_code = (reg2 >> 24) & 0xFF; // 3rd word

  device->header_type = (reg3 >> 16) & 0x7f; // lowest 7-bits
  device->multi_function = (reg3 >> 23) & 0x1; // eighth bit

  device->interrupt_line = reg15 & 0xFF; // 1st word
  device->interrupt_pin = (reg15 >> 8) & 0xFF; // 2nd word

  device->next = NULL;

  if (device->header_type == 0x00) {
    // standard header
    device->bar_count = 6;
  } else if (device->header_type == 0x01) {
    // pci-to-pci bridge header
    device->bar_count = 2;
  } else if (device->header_type == 0x01) {
    // pci-to-cardbus bridge header
    device->bar_count = 0;
  }

  // Read all of the device BARs
  pci_bar_t *bars = kcalloc(sizeof(pci_bar_t), device->bar_count);
  for (int i = 0; i < device->bar_count; i++) {
    uint8_t offset = 0x10 + (i * 0x04);
    // read bar value
    uint32_t bar = pci_read(addr, offset);
    // write all 1s to load register with bar size
    pci_write(addr, offset, 0xFFFFFFFF);
    // read bar size
    uint32_t size = pci_read(addr, offset);
    // write back original bar value
    pci_write(addr, offset, bar);

    if (bar & 0x1) {
      // i/o space bar
      size &= 0xFFFFFFFC;
      uint32_t addr_start = bar & 0xFFFFFFFC;
      uint32_t addr_end = addr_start + (~(size) + 1);

      bars[i].bar_type = 1;
      bars[i].io.addr_start = addr_start;
      bars[i].io.addr_end = addr_end;
    } else {
      // memory space bar
      size &= 0xFFFFFFF0;
      uint8_t addr_type = (bar >> 1) & 0x3;
      uint8_t prefetch = (bar >> 3) & 0x1;
      uint32_t addr_start = bar & 0xFFFFFFF0;
      uint32_t addr_end = addr_start + (~(size) + 1);

      bars[i].bar_type = 0;
      bars[i].mem.addr_type = addr_type;
      bars[i].mem.prefetch = prefetch;
      bars[i].mem.addr_start = addr_start;
      bars[i].mem.addr_end = addr_end;
    }
  }

  device->bars = bars;
}

//

pci_device_t *pci_alloc_device_t(uint8_t bus, uint8_t device, uint8_t function) {
  pci_device_t *dev = kmalloc(sizeof(pci_device_t));
  memset(dev, 0, sizeof(pci_device_t));
  dev->bus = bus;
  dev->device = device;
  dev->function = function;
  return dev;
}

void pci_free_device_t(pci_device_t *device) {
  if (device->bar_count > 0) {
    kfree(device->bars);
  }

  pci_device_t *curr = device;
  while (curr) {
    pci_device_t *next = curr->next;
    kfree(curr);
    curr = next;
  }
}

//

int pci_probe_device(pci_device_t *device, pci_callback_t callback, void *context) {
  pci_read_device_info(device);
  if (is_device_valid(device)) {
    // check if device is pci-to-pci bridge
    if (device->class_code == PCI_BRIDGE && device->subclass_code == PCI_PCI_BRIDGE) {
      uint32_t addr = device_config_address(device);
      uint32_t reg6 = pci_read(addr, 0x18);
      uint8_t secondary_bus = (reg6 >> 8) & 0xFF;
      kprintf("probing secondary bus\n");
      // probe the bridged bus
      int result = pci_probe_bus(secondary_bus, callback, context);
      if (result == PCI_PROBE_STOP) {
        return PCI_PROBE_STOP;
      }
    }

    if (callback) {
      int result = callback(device, context);
      if (result == PCI_PROBE_STOP) {
        return PCI_PROBE_STOP;
      }
    }

    // check other device functions
    if (device->multi_function == 1 && device->function == 0) {
      pci_device_t *last = device;
      for (int i = 1; i < 8; i++) {
        pci_device_t *func = pci_alloc_device_t(device->bus, device->device, i);
        int result = pci_probe_device(func, callback, context);
        if (result == PCI_PROBE_STOP) {
          return PCI_PROBE_STOP;
        }

        if (!is_device_valid(func)) {
          pci_free_device_t(func);
          break;
        };

        last->next = func;
        last = func;
      }
    }
  }
  return PCI_PROBE_CONTINUE;
}

int pci_probe_bus(uint8_t bus, pci_callback_t callback, void *context) {
  for (int i = 0; i < 32; i++) {
    pci_device_t *device = pci_alloc_device_t(bus, i, 0);
    int result = pci_probe_device(device, callback, context);
    if (result == PCI_PROBE_STOP) {
      return PCI_PROBE_STOP;
    } else {
      pci_free_device_t(device);
    }
  }

  return PCI_PROBE_CONTINUE;
}

void pci_probe_busses(pci_callback_t callback, void *context) {
  uint32_t addr = config_address(0, 0, 0);
  uint32_t reg3 = pci_read(addr, 0x0C);
  uint8_t header_type = (reg3 >> 16) & 0x7F;

  if (header_type == 0x00) {
    pci_probe_bus(0, callback, context);
  } else if (header_type == 0x01) {
    // Multiple PCI host controllers
    for (int i = 0; i < 8; i++) {
      int result = pci_probe_bus(i, callback, context);
      if (result == PCI_PROBE_STOP) {
        break;
      }
    }
  }
}

/* ---- pci enumerate busses ---- */

int pci_enumerate_busses_cb(pci_device_t *device, void *context) {
  pci_print_debug_device(device);
  return PCI_PROBE_CONTINUE;
}

void pci_enumerate_busses() {
  pci_probe_busses(pci_enumerate_busses_cb, NULL);
}

/* ---- pci locate device ---- */

int pci_locate_device_cb(pci_device_t *device, void *context) {
  struct locate_context *ctx = context;

  if (device->class_code == ctx->device_class &&
      device->subclass_code == ctx->device_subclass) {
    kprintf("device located!\n");
    *ctx->result = device;
    return PCI_PROBE_STOP;
  }
  return PCI_PROBE_CONTINUE;
}

pci_device_t *pci_locate_device(uint8_t device_class, uint8_t device_subclass) {
  pci_device_t *result;
  struct locate_context ctx = { device_class, device_subclass, &result };
  pci_probe_busses(pci_locate_device_cb, &ctx);
  return *ctx.result;
}

// Debugging

void pci_print_debug_command(pci_command_t *command) {
  kprintf("command = {\n"
          "  io_space = %d \n"
          "  mem_space = %d \n"
          "  bus_master = %d \n"
          "  special_cycles = %d \n"
          "  mem_write_invld = %d \n"
          "  vga_palette_snoop = %d \n"
          "  parity_err_resp = %d \n"
          "  serr_enable = %d \n"
          "  fast_b2b_enable = %d \n"
          "  int_disable = %d \n"
          "}\n",
          command->io_space,
          command->mem_space,
          command->bus_master,
          command->special_cycles,
          command->mem_write_invld,
          command->vga_palette_snoop,
          command->parity_err_resp,
          command->serr_enable,
          command->fast_b2b_enable,
          command->int_disable);
}

void pci_print_debug_status(pci_status_t *status) {
  kprintf("status = {\n"
          "  int_status = %d \n"
          "  cap_list = %d \n"
          "  dev_freq = %d \n"
          "  fast_b2b = %d \n"
          "  master_parity = %d \n"
          "  devsel_timing = %d \n"
          "  sig_target_abrt = %d \n"
          "  recv_target_abrt = %d \n"
          "  recv_master_abrt = %d \n"
          "  sig_system_err = %d \n"
          "  det_parity_err = %d \n"
          "}\n",
          status->int_status,
          status->cap_list,
          status->dev_freq,
          status->fast_b2b,
          status->master_parity,
          status->devsel_timing,
          status->sig_target_abrt,
          status->recv_target_abrt,
          status->recv_master_abrt,
          status->sig_system_err,
          status->det_parity_err);
}

void pci_print_debug_device(pci_device_t *device) {
  kprintf("Bus %d, device %d, function %d:\n", device->bus, device->device, device->function);
  kprintf("  %s: PCI device %x:%x\n",
          pci_get_subclass_desc(device->class_code, device->subclass_code),
          device->vendor_id,
          device->device_id);

  kprintf("  Prog IF: 0x%X\n", device->prog_if);

  if (device->interrupt_pin > 0) {
    kprintf("  IRQ %d, pin %c\n", device->interrupt_line, device->interrupt_pin + 64);
  }

  for (int i = 0; i < device->bar_count; i++) {
    pci_bar_t bar = device->bars[i];

    if (bar.bar_type == 1) {
      kprintf("  BAR%d: I/O at %#x [%#x]\n", i, bar.io.addr_start, bar.io.addr_end - 1);
    } else {
      if (bar.mem.addr_start == 0) {
        continue;
      }

      const char *prefetch = bar.mem.prefetch ? "prefetchable" : "\b";
      const char *addr_type;
      switch (bar.mem.addr_type) {
        case 0: addr_type = "32"; break;
        case 1: addr_type = "16"; break;
        case 2: addr_type = "64"; break;
        default: addr_type = "32";
      }

      kprintf("  BAR%d: %s bit %s memory at %p [%p]\n",
              i, addr_type, prefetch, bar.mem.addr_start, bar.mem.addr_end - 1);
    }
  }
}
