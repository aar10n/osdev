//
// Created by Aaron Gill-Braun on 2023-02-05.
//

#include <kernel/bus/pci_v2.h>
#include <kernel/bus/pci_hw.h>
#include <kernel/bus/pci_tables.h>

#include <kernel/device.h>
#include <kernel/mm.h>
#include <kernel/init.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("pci: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("pci: %s: " fmt, __func__, ##__VA_ARGS__)

#define MAX_PCIE_SEG_GROUPS 1
#define PCIE_MMIO_SIZE (256 * SIZE_1MB)

#define is_mem_bar_valid(b) (((b) & 0xFFFFFFF0) == 0 ? (((b) >> 1) & 0b11) : true)
#define is_io_bar_valid(b) (((b) & 0xFFFFFFFC) != 0)
#define is_bar_valid(bar) (((bar) & 1) == 0 ? is_mem_bar_valid(bar) : is_io_bar_valid(bar))

#define msi_msg_addr(cpu) (0xFEE00000 | ((cpu) << 12))
#define msi_msg_data(vec, e, d) \
  (((vec) & 0xFF) | ((e) == 1 ? 0 : (1 << 15)) | ((d) == 1 ? 0 : (1 << 14)))

#define MASK_PTR(ptr) ((ptr) & 0xFFFFFFFFFFFFFFFC)

typedef struct pci_bus_type {
  device_bus_t bus_type;
  struct pci_segment_group *segment_group;
} pci_bus_type_t;

struct pci_segment_group {
  uint8_t num;
  uint8_t bus_start;
  uint8_t bus_end;

  uintptr_t phys_addr;
  uintptr_t address;

  LIST_ENTRY(struct pci_segment_group) list;
};

static size_t num_segment_groups = 0;
static LIST_HEAD(struct pci_segment_group) segment_groups;

static void remap_pcie_address_space(void *data) {
  struct pci_segment_group *seg = data;
  seg->address = vmap_phys(seg->phys_addr, 0, PCIE_MMIO_SIZE, VM_RDWR|VM_NOCACHE|VM_HUGE_2MB, "pcie");
}

static struct pci_segment_group *get_segment_group_for_bus_number(uint8_t bus) {
  struct pci_segment_group *group;
  LIST_FOREACH(group, &segment_groups, list) {
    if (bus >= group->bus_start && bus <= group->bus_end) {
      return group;
    }
  }
  return NULL;
}

static void *pci_device_address(struct pci_segment_group *group, uint8_t bus, uint8_t device, uint8_t function) {
  kassert(bus >= group->bus_start && bus <= group->bus_end);
  return ((void *)(group->address | ((uint64_t)(bus - group->bus_start) << 20) |
                   ((uint64_t)(device) << 15) | ((uint64_t)(function) << 12)));
}

static pci_bar_t *get_device_bars(uint32_t *bar_ptr, int bar_count) {
  pci_bar_t *first = NULL;
  pci_bar_t *bars = NULL;
  for (int i = 0; i < bar_count; i++) {
    uint32_t v32 = bar_ptr[i];

    // ensure it is a non-empty bar
    if (is_bar_valid(v32)) {
      volatile uint32_t *b32 = &(bar_ptr[i]);

      pci_bar_t *bar = kmalloc(sizeof(pci_bar_t));
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

      bar->next = NULL;
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

static pci_cap_t *get_device_caps(uintptr_t config_base, uintptr_t cap_off) {
  pci_cap_t *first = NULL;
  pci_cap_t *caps = NULL;

  uint16_t *ptr = (void *)(config_base + cap_off);
  while (true) {
    uint8_t id = *ptr & 0xFF;
    uint8_t next = MASK_PTR(*ptr >> 8);

    if (id > 0x15) {
      break;
    } else if (id == 0) {
      continue;
    }

    pci_cap_t *cap = kmalloc(sizeof(pci_cap_t));
    cap->id = id;
    cap->offset = (uintptr_t) ptr;
    cap->next = NULL;

    if (caps != NULL) {
      caps->next = cap;
    } else {
      first = cap;
    }
    caps = cap;

    if (next == 0) {
      break;
    }
    ptr = offset_ptr(config_base, next);
  }
  return first;
}

//

void register_pci_segment_group(uint16_t number, uint8_t start_bus, uint8_t end_bus, uintptr_t address) {
  if (number >= MAX_PCIE_SEG_GROUPS || num_segment_groups >= MAX_PCIE_SEG_GROUPS) {
    EPRINTF("ignoring pci segment group %d, not supported\n", number);
    return;
  } else if (get_segment_group_for_bus_number(start_bus) != NULL) {
    panic("pci segment group %d already registered", number);
  }

  struct pci_segment_group *group = kmallocz(sizeof(struct pci_segment_group));
  group->num = number;
  group->bus_start = start_bus;
  group->bus_end = end_bus;
  group->phys_addr = address;
  group->address = address;

  LIST_ADD(&segment_groups, group, list);
  num_segment_groups++;

  register_init_address_space_callback(remap_pcie_address_space, group);
}

void pci_enable_msi_vector(pci_device_t *pci_dev, uint8_t index, uint8_t vector) {
  pci_cap_t *msix_cap_ptr = SLIST_FIND(c, pci_dev->caps, next, c->id == PCI_CAP_MSIX);
  if (msix_cap_ptr == NULL) {
    panic("pci: could not locate msix capability");
  }

  pci_cap_msix_t *msix_cap = (void *) msix_cap_ptr->offset;
  uint16_t tbl_size = msix_cap->tbl_sz + 1;
  ASSERT(index < tbl_size);

  pci_bar_t *bar = SLIST_FIND(b, pci_dev->bars, next, b->num == msix_cap->bir);
  if (bar == NULL) {
    panic("pci: could not locate msix bar");
  } else if (bar->virt_addr == 0) {
    panic("pci: msix memory space not mapped");
  }

  volatile pci_msix_entry_t *table = (void *)(bar->virt_addr + (msix_cap->tbl_ofst << 3));
  pci_msix_entry_t *entry = &table[index];

  entry->msg_addr = msi_msg_addr(PERCPU_ID);
  entry->msg_data = msi_msg_data(vector, 1, 0);
  entry->masked = 0;
  msix_cap->en = 1;
}

void pci_disable_msi_vector(pci_device_t *pci_dev, uint8_t index) {
  pci_cap_t *msix_cap_ptr = SLIST_FIND(c, pci_dev->caps, next, c->id == PCI_CAP_MSIX);
  if (msix_cap_ptr == NULL) {
    panic("pci: could not locate msix capability");
  }

  pci_cap_msix_t *msix_cap = (void *) msix_cap_ptr->offset;
  uint16_t tbl_size = msix_cap->tbl_sz + 1;
  kassert(index < tbl_size);

  pci_bar_t *bar = SLIST_FIND(b, pci_dev->bars, next, b->num == msix_cap->bir);
  if (bar == NULL) {
    panic("pci: could not locate msix bar");
  } else if (bar->virt_addr == 0) {
    panic("pci: msix memory space not mapped");
  }

  volatile pci_msix_entry_t *table = (void *)(bar->virt_addr + (msix_cap->tbl_ofst << 3));
  table[index].masked = 1;
}

//
// MARK: Device Bus API
//

static int pci_bus_probe(struct device_bus *bus) {
  pci_bus_type_t *pci_bus = bus->data;
  struct pci_segment_group *seg = pci_bus->segment_group;

  DPRINTF("probing segment group %d\n", seg->num);
  for (int bus_num = seg->bus_start; bus_num < seg->bus_end; bus_num++) {
    // probe devices on bus
    for (int d = 0; d < 32; d++) { // device
      for (int f = 0; f < 8; f++) { // function
        struct pci_header *header = pci_device_address(seg, bus_num, d, f);
        if (header->vendor_id == 0xFFFF || header->type != 0) {
          continue;
        }

        if (header->class_code == PCI_BRIDGE_DEVICE) {
          continue;
        }

        struct pci_header_normal *config = (void *) header;
        DPRINTF("found device on bus %d: %02X.%X: %s\n", bus_num, d, f, pci_get_device_desc(header->class_code, header->subclass, header->prog_if));
        DPRINTF("    (%02X.%02x.%02X)\n", header->class_code, header->subclass, header->prog_if);

        struct pci_device *dev = kmallocz(sizeof(struct pci_device));
        dev->bus = bus_num;
        dev->device = d;
        dev->function = f;
        dev->class_code = header->class_code;
        dev->subclass = header->subclass;
        dev->prog_if = header->prog_if;

        dev->device_id = header->device_id;
        dev->vendor_id = header->vendor_id;

        dev->int_line = config->int_line;
        dev->int_pin = config->int_pin;
        dev->subsystem = config->subsys_id;
        dev->subsystem_vendor = config->subsys_vendor_id;

        dev->bars = get_device_bars(config->bars, 6);
        dev->caps = get_device_caps((uintptr_t) header, MASK_PTR(config->cap_ptr));

        if (register_bus_device(bus, dev) < 0) {
          EPRINTF("failed to register device %02X:%02X.%X\n", bus_num, d, f);
          kfree(dev);
        }

        if (!header->multifn) {
          break;
        }
      }
    }

    // TODO: figure out why there are extra busses that are duplicated
    break;
  }
  return 0;
}

void module_init_pci_register_bus() {
  LIST_FOR_IN(seg, &segment_groups, list) {
    struct pci_bus_type *bus = kmalloc(sizeof(struct pci_bus_type));
    memset(bus, 0, sizeof(struct pci_bus_type));

    device_bus_t bus_type = {
      .name = "pci",
      .probe = pci_bus_probe,
      .data = bus,
    };
    bus->bus_type = bus_type;
    bus->segment_group = seg;
    mtx_init(&bus->bus_type.devices_lock, 0, "bus_devices_lock");

    if (register_bus(&bus->bus_type) < 0) {
      panic("pci: failed to register bus");
    }
  }
}
MODULE_INIT(module_init_pci_register_bus);
