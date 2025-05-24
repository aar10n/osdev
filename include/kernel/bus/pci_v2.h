//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef KERNEL_BUS_PCI_V2_H
#define KERNEL_BUS_PCI_V2_H

#include <kernel/base.h>
#include <kernel/queue.h>

// Device Classes
#define PCI_STORAGE_CONTROLLER    0x01
#define PCI_NETWORK_CONTROLLER    0x02
#define PCI_DISPLAY_CONTROLLER    0x03
#define PCI_BRIDGE_DEVICE         0x06
#define PCI_BASE_PERIPHERAL       0x08
#define PCI_SERIAL_BUS_CONTROLLER 0x0C

// Mass Storage Controllers
#define PCI_SCSI_BUS_CONTROLLER 0x00
#define PCI_IDE_CONTROLLER 0x01
#define PCI_FLOPPY_DISK_CONTROLLER 0x02
#define PCI_ATA_CONTROLLER 0x05
#define PCI_SERIAL_ATA_CONTROLLER 0x06

// Network Controllers
#define PCI_ETHERNET_CONTROLLER 0x00

// Display Controllers
#define PCI_VGA_CONTROLLER 0x00

// Bridge Devices
#define PCI_HOST_BRIDGE 0x00
#define PCI_ISA_BRIDGE 0x01
#define PCI_PCI_BRIDGE 0x04

// Serial Bus Controllers
#define PCI_USB_CONTROLLER 0x03

#define USB_PROG_IF_UHCI 0x00
#define USB_PROG_IF_OHCI 0x10
#define USB_PROG_IF_EHCI 0x20 // USB2
#define USB_PROG_IF_XHCI 0x30 // USB3


// Capability Types
#define PCI_CAP_MSI      0x05
#define PCI_CAP_MSIX     0x11


typedef struct pci_bar {
  uint8_t num: 3;      // bar number
  uint8_t kind: 1;     // bar kind (0 = mem, 1 = io)
  uint8_t type: 2;     // memory type (0 = 32-bit, 1 = 64-bit, 2 = 64-bit 32-bit)
  uint8_t prefetch: 1; // prefetchable (only for mem)
  uint8_t : 1;          // reserved
  uint64_t phys_addr;   // base physical address
  uint64_t virt_addr;   // virtual address
  uint64_t size;        // memory size
  struct pci_bar *next; // next bar
} pci_bar_t;

typedef struct pci_cap {
  uint8_t id;           // capability id
  uintptr_t offset;     // offset to cap
  struct pci_cap *next; // next cap
} pci_cap_t;

typedef struct pci_device {
  uint8_t bus;
  uint8_t device: 5;
  uint8_t function: 3;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;

  uint16_t device_id;
  uint16_t vendor_id;

  uint8_t int_line;
  uint8_t int_pin;
  uint16_t subsystem;
  uint16_t subsystem_vendor;

  pci_bar_t *bars;
  pci_cap_t *caps;

  bool registered;
} pci_device_t;

void register_pci_segment_group(uint16_t number, uint8_t start_bus, uint8_t end_bus, uintptr_t address);

#endif
