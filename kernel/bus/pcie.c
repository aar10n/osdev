//
// Created by Aaron Gill-Braun on 2021-02-18.
//

#include <bus/pcie.h>
#include <mm.h>
#include <printf.h>
#include <panic.h>
#include <system.h>
#include <string.h>
#include <bus/pci_tables.h>

#define PCIE_BASE (system_info->pcie->virt_addr)
#define pcie_addr(bus, device, function) \
  ((void *)(PCIE_BASE | ((bus) << 20) | ((device) << 15) | ((function) << 12)))

#define is_mem_bar_valid(b) (((b) & 0xFFFFFFF0) == 0 ? (((b) >> 1) & 0b11) : true)
#define is_io_bar_valid(b) (((b) & 0xFFFFFFFC) != 0)
#define is_bar_valid(bar) (((bar) & 1) == 0 ? is_mem_bar_valid(bar) : is_io_bar_valid(bar))

#define msi_msg_addr(cpu) (0xFEE00000 | (cpu << 12))
#define msi_msg_data(vec, e, d) \
  (((vec) & 0xFF) | ((e) == 1 ? 0 : (1 << 15)) | ((d) == 1 ? 0 : (1 << 14)))


static pcie_list_head_t *devices[16] = {};


pcie_list_head_t *alloc_list_head(pcie_device_t *device) {
  pcie_list_head_t *list = kmalloc(sizeof(pcie_list_head_t));
  list->class_code = device->class_code;
  list->subclass = device->subclass;
  list->first = device;
  list->last = device;
  list->next = NULL;
  return list;
}

pcie_bar_t *get_device_bars(uint32_t *bar_ptr, int bar_count) {
  pcie_bar_t *first = NULL;
  pcie_bar_t *bars = NULL;
  for (int i = 0; i < bar_count; i++) {
    uint32_t v32 = bar_ptr[i];

    // ensure it is a non-empty bar
    if (is_bar_valid(v32)) {
      volatile uint32_t *b32 = &(bar_ptr[i]);

      pcie_bar_t *bar = kmalloc(sizeof(pcie_bar_t));
      bar->num = i;
      bar->virt_addr = 0;
      if ((v32 & 1) == 0) {
        // memory bar
        bar->kind = 0;
        bar->type = (v32 >> 1) & 3;
        bar->prefetch = (v32 >> 3) & 1;
        if (bar->type == 0) {
          // 32-bit bar
          *b32 = UINT32_MAX;
          bar->phys_addr = v32 & ~0xF;
          bar->size = ~(*b32 & ~0xF) + 1;
          bar->next = NULL;
          *b32 = v32;
        } else if (bar->type == 2) {
          // 64-bit bar
          volatile uint64_t *b64 = (uint64_t *) &bar_ptr[i];
          uint64_t v64 = *b64;

          *b64 = UINT64_MAX;
          bar->phys_addr = v64 & ~0xF;
          bar->size = ~(*b64 & ~0xF) + 1;
          *b64 = v64;

          i++;
        }
      } else {
        // io bar
        bar->kind = 1;
        bar->type = 0;
        bar->prefetch = 0;

        *b32 = UINT32_MAX;
        bar->phys_addr = v32 & ~0x3;
        bar->size = ~(*b32 & ~0x3) + 1;
        *b32 = v32;
      }

      if (bars != NULL) {
        bars->next = bar;
      } else {
        first = bar;
      }
      bars = bar;
    }
  }
  return first;
}

pcie_cap_t *get_device_caps(uintptr_t config_base, uint16_t cap_offset) {

  pcie_cap_t *first = NULL;
  pcie_cap_t *caps = NULL;

  uint16_t *ptr = (void *)(config_base + cap_offset);
  uint16_t *ptr_max = align_ptr(ptr, 4096);
  while (ptr < ptr_max) {
    uint8_t id = *ptr & 0xFF;
    uint8_t next = *ptr >> 8;

    if (next == 0 || id > 0x15) {
      break;
    } else if (id == 0) {
      continue;
    }

    pcie_cap_t *cap = kmalloc(sizeof(pcie_cap_t));
    cap->id = id;
    cap->offset = (uintptr_t) ptr - config_base;
    cap->next = NULL;

    if (caps != NULL) {
      caps->next = cap;
    } else {
      first = cap;
    }
    caps = cap;
    ptr = offset_ptr(ptr, next);
  }
  return first;
}

void add_device(pcie_device_t *device) {
  pcie_list_head_t *list = devices[device->class_code];
  if (list == NULL) {
    list = alloc_list_head(device);
    devices[device->class_code] = list;
  } else {
    while (list->next != NULL) {
      if (list->subclass == device->subclass) {
        break;
      }
      list = list->next;
    }

    if (list->subclass != device->subclass) {
      list->next = alloc_list_head(device);
    } else {
      list->last->next = device;
      list->last = device;
    }
  }
}

//

void pcie_init() {
  unreachable;
  pcie_desc_t *pcie = system_info->pcie;
  pcie->virt_addr = (uintptr_t) _vmap_mmio(pcie->phys_addr, PCIE_MMIO_SIZE, PG_WRITE | PG_NOCACHE);
  memset(devices, 0, sizeof(devices));
}


void pcie_discover() {
  kprintf("[pcie] discovering devices...\n");

  int bus = 0;
  for (int device = 0; device < 32; device++) {
    for (int function = 0; function < 8; function++) {
      pcie_header_t *header = pcie_addr(bus, device, function);
      if (header->vendor_id == 0xFFFF || header->type != 0) {
        continue;
      }

      pcie_header_normal_t *config = (void *) header;
      pcie_device_t *dev = kmalloc(sizeof(pcie_device_t));

      dev->device_id = header->device_id;
      dev->vendor_id = header->vendor_id;

      dev->bus = bus;
      dev->device = device;
      dev->function = function;

      dev->class_code = header->class_code;
      dev->subclass = header->subclass;
      dev->prog_if = header->prog_if;
      dev->int_line = config->int_line;
      dev->int_pin = config->int_pin;

      dev->subsystem = config->subsys_id;
      dev->subsystem_vendor = config->subsys_vendor_id;

      dev->bars = get_device_bars(config->bars, 6);
      dev->caps = get_device_caps((uintptr_t) header, config->cap_ptr);
      dev->base_addr = (uintptr_t) header;
      dev->next = NULL;

      add_device(dev);
      pcie_print_device(dev);

      if (!header->multifn) {
        break;
      }
    }
  }
}

pcie_device_t *pcie_locate_device(uint8_t class_code, uint8_t subclass, int prog_if) {
  pcie_list_head_t *list = devices[class_code];
  if (list == NULL) {
    return NULL;
  }

  while (list && list->subclass != subclass) {
    list = list->next;
  }
  if (list == NULL) {
    return NULL;
  }

  if (prog_if >= 0) {
    pcie_device_t *device = list->first;
    while (device && device->prog_if != prog_if) {
      device = device->next;
    }
    return device;
  } else {
    return list->first;
  }
}

pcie_bar_t *pcie_get_bar(pcie_device_t *device, int bar_num) {
  kassert(bar_num >= 0 && bar_num < 6);

  pcie_bar_t *bar = device->bars;
  while (bar && bar->num != bar_num) {
    bar = bar->next;
  }
  return bar;
}

void *pcie_get_cap(pcie_device_t *device, int cap_id) {
  pcie_cap_t *pci_cap = device->caps;
  while (pci_cap != NULL) {
    if (pci_cap->id == cap_id) {
      break;
    }
    pci_cap = pci_cap->next;
  }
  if (pci_cap == NULL) {
    return NULL;
  }
  return (void *)(device->base_addr + pci_cap->offset);
}

//

// void pcie_enable_msi_vector(pcie_device_t *device, uint8_t index, uint8_t vector) {
//   pcie_cap_msix_t *msi_cap = pcie_get_cap(device, PCI_CAP_MSI);
//   if (msi_cap == NULL) {
//     panic("[pcie] could not locate msi structure");
//   }
//
//   uint16_t tbl_size = msi_cap->tbl_sz + 1;
//   kassert(index < tbl_size);
//
//   pcie_bar_t *bar = pcie_get_bar(device, msix_cap->bir);
//   if (bar->virt_addr == 0) {
//     panic("[pcie] msix memory space not mapped");
//   }
//
//   pcie_msix_entry_t *table = (void *)(bar->virt_addr + (msix_cap->tbl_ofst << 3));
//   pcie_msix_entry_t *entry = &table[index];
//   entry->msg_addr = msi_msg_addr(PERCPU->id);
//   entry->msg_data = msi_msg_data(vector, 0, 0);
//   entry->masked = 0;
//
//   msix_cap->en = 1;
// }

void pcie_enable_msi_vector(pcie_device_t *device, uint8_t index, uint8_t vector) {
  pcie_cap_msix_t *msix_cap = pcie_get_cap(device, PCI_CAP_MSIX);
  if (msix_cap == NULL) {
    panic("[pcie] could not locate msix structure");
  }

  uint16_t tbl_size = msix_cap->tbl_sz + 1;
  kassert(index < tbl_size);

  pcie_bar_t *bar = pcie_get_bar(device, msix_cap->bir);
  if (bar->virt_addr == 0) {
    panic("[pcie] msix memory space not mapped");
  }

  pcie_msix_entry_t *table = (void *)(bar->virt_addr + (msix_cap->tbl_ofst << 3));
  pcie_msix_entry_t *entry = &table[index];

  entry->msg_addr = msi_msg_addr(PERCPU->id);
  entry->msg_data = msi_msg_data(vector, 1, 0);
  entry->masked = 0;

  msix_cap->en = 1;
}

void pcie_disable_msi_vector(pcie_device_t *device, uint8_t index) {
  pcie_cap_msix_t *msix_cap = pcie_get_cap(device, PCI_CAP_MSIX);
  if (msix_cap == NULL) {
    panic("[pcie] could not locate msix structure");
  }

  uint16_t tbl_size = msix_cap->tbl_sz + 1;
  kassert(index < tbl_size);

  pcie_bar_t *bar = pcie_get_bar(device, msix_cap->bir);
  if (bar->virt_addr == 0) {
    panic("[pcie] msix memory space not mapped");
  }

  pcie_msix_entry_t *table = (void *)(bar->virt_addr + (msix_cap->tbl_ofst << 3));
  table[index].masked = 0;
}

//

void pcie_print_device(pcie_device_t *device) {
  kprintf("Bus %d, device % 2d, function: %d:\n",
          device->bus, device->device, device->function);

  kprintf("  %s: PCI device %x:%x\n",
          pci_get_subclass_desc(device->class_code, device->subclass),
          device->vendor_id, device->device_id);

  kprintf("    PCI subsystem %x:%x\n", device->subsystem_vendor, device->subsystem);

  if (device->int_line != 0xFF) {
    kprintf("    IRQ %d, pin %c\n", device->int_line, device->int_pin + '@');
  }

  pcie_bar_t *bar = device->bars;
  while (bar != NULL) {
    const char *mem_type = bar->type == 2 ? "64" : "32";
    const char *prefetch = bar->prefetch ? "prefetchable " : "";

    if (bar->kind == 0) {
      kprintf("    BAR%d: %s bit %smemory at %p [%p]\n",
              bar->num, mem_type, prefetch, bar->phys_addr, bar->phys_addr + bar->size - 1);
    } else {
      kprintf("    BAR%d: I/O at %p [%p]\n",
              bar->num, bar->phys_addr, bar->phys_addr + bar->size - 1);
    }

    bar = bar->next;
  }
}
