//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#ifndef KERNEL_USB_XHCI_H
#define KERNEL_USB_XHCI_H

#include <base.h>
#include <usb/xhci_hw.h>
#include <usb/usb.h>
#include <bus/pcie.h>
#include <mm.h>
#include <mutex.h>

// xhci controller structure
typedef struct xhci_dev {
  pcie_device_t *pci_dev;
  uintptr_t phys_addr;
  uintptr_t virt_addr;
  size_t size;

  uintptr_t cap_base;
  uintptr_t op_base;
  uintptr_t rt_base;
  uintptr_t db_base;
  uintptr_t xcap_base;

  uintptr_t *dcbaap;

  // threads
  thread_t *event_thread;

  // conditions
  cond_t init;
  cond_t event;
  cond_t event_ack;

  xhci_protocol_t *protocols;
  xhci_port_t *ports;

  xhci_intrptr_t *intr;
  xhci_ring_t *cmd_ring;
} xhci_dev_t;


void xhci_init();
void xhci_setup_devices();

void xhci_queue_command(xhci_trb_t *trb);
void *xhci_run_command(xhci_trb_t *trb);
void xhci_run_commands();

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t *setup, uint8_t type);
int xhci_queue_data(xhci_device_t *device, void *buffer, uint16_t size, bool dir);
int xhci_queue_status(xhci_device_t *device, bool dir);
int xhci_run_device(xhci_device_t *device);

void xhci_get_descriptor(xhci_device_t *device);

#endif
