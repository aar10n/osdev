//
// Created by Aaron Gill-Braun on 2023-02-05.
//

#include <bus/pci_v2.h>
#include <bus/pci_hw.h>

#include <device.h>
#include <mm.h>
#include <init.h>
#include <panic.h>
#include <printf.h>
#include <string.h>

#define MAX_PCIE_SEG_GROUPS 1
#define PCIE_MMIO_SIZE (256 * SIZE_1MB)

#define is_mem_bar_valid(b) (((b) & 0xFFFFFFF0) == 0 ? (((b) >> 1) & 0b11) : true)
#define is_io_bar_valid(b) (((b) & 0xFFFFFFFC) != 0)
#define is_bar_valid(bar) (((bar) & 1) == 0 ? is_mem_bar_valid(bar) : is_io_bar_valid(bar))

#define msi_msg_addr(cpu) (0xFEE00000 | ((cpu) << 12))
#define msi_msg_data(vec, e, d) \
  (((vec) & 0xFF) | ((e) == 1 ? 0 : (1 << 15)) | ((d) == 1 ? 0 : (1 << 14)))

#define MASK_PTR(ptr) ((ptr) & 0xFFFFFFFFFFFFFFFC)

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
  seg->address = (uintptr_t) vm_alloc_map_phys(seg->phys_addr, 0, PCIE_MMIO_SIZE, 0, PG_BIGPAGE|PG_WRITE|PG_NOCACHE, "pcie");
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

//
//
//

// called from acpi
void register_pci_segment_group(uint16_t number, uint8_t start_bus, uint8_t end_bus, uintptr_t address) {
  if (number >= MAX_PCIE_SEG_GROUPS || num_segment_groups >= MAX_PCIE_SEG_GROUPS) {
    kprintf("pci: ignoring pci segment group %d, not supported\n", number);
    return;
  } else if (get_segment_group_for_bus_number(start_bus) != NULL) {
    panic("pci segment group %d already registered", number);
  }

  struct pci_segment_group *group = kmalloc(sizeof(struct pci_segment_group));
  group->num = number;
  group->bus_start = start_bus;
  group->bus_end = end_bus;
  group->phys_addr = address;
  group->address = address;

  LIST_ADD(&segment_groups, group, list);
  num_segment_groups++;

  register_init_address_space_callback(remap_pcie_address_space, group);
}

//

typedef struct pci_bus_type {
  device_bus_t bus_type;
  struct pci_segment_group *segment_group;
} pci_bus_type_t;

static int pci_bus_probe(struct device_bus *bus) {
  pci_bus_type_t *pci_bus = bus->data;
  struct pci_segment_group *seg = pci_bus->segment_group;

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
        struct pci_device *dev = kmalloc(sizeof(struct pci_device));
        dev->device_id = header->device_id;
        dev->vendor_id = header->vendor_id;
        dev->bus = bus_num;
        dev->device = d;
        dev->function = f;

        dev->class_code = header->class_code;
        dev->subclass = header->subclass;
        dev->prog_if = header->prog_if;
        dev->int_line = config->int_line;
        dev->int_pin = config->int_pin;

        dev->subsystem = config->subsys_id;
        dev->subsystem_vendor = config->subsys_vendor_id;

        //dev->bars = get_device_bars(config->bars, 6);
        //dev->caps = get_device_caps((uintptr_t) header, MASK_PTR(config->cap_ptr));
        //dev->base_addr = (uintptr_t) header;
        if (register_bus_device(bus, dev) < 0) {
          kprintf("pci: failed to register device %d:%d:%d\n", bus_num, d, f);
          kfree(dev);
        }

        if (!header->multifn) {
          break;
        }
      }
    }
  }
  return 0;
}

void pci_module_init() {
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
    if (register_bus(&bus->bus_type) < 0) {
      panic("pci: failed to register bus");
    }
  }
}
MODULE_INIT(pci_module_init);
