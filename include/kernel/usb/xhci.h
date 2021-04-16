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

typedef struct {
  xhci_trb_t *ptr;    // ring base
  page_t *page;       // ring pages
  uint32_t index;     // ring enqueue/dequeue index
  uint32_t max_index; // max index
  bool ccs;           // cycle state
} xhci_ring_t;

typedef struct xhci_protocol {
  uint8_t rev_major;   // major usb revision
  uint8_t rev_minor;   // minor usb revision
  uint8_t port_offset; // compatible port offset
  uint8_t port_count;  // compatible port count
  struct xhci_protocol *next;
} xhci_protocol_t;

typedef struct {
  uint8_t number;    // interrupter number
  uint8_t vector;    // mapped interrupt vector
  uintptr_t erst;    // event ring segment table
  xhci_ring_t *ring; // event ring
} xhci_intrptr_t;

typedef struct {
  uint8_t slot_id;
  uint8_t port_num;

  xhci_ring_t *ring;
  page_t *input_page;
  xhci_input_ctx_t *input;
  page_t *output_page;
  xhci_device_ctx_t *output;

  usb_device_descriptor_t *desc;
  usb_config_descriptor_t **configs;
} xhci_device_t;

typedef struct xhci_port {
  uint8_t number;            // port number
  xhci_protocol_t *protocol; // port protocol
  xhci_device_t *device;     // attached device
  struct xhci_port *next;    // linked list
} xhci_port_t;

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

int xhci_init_controller(xhci_dev_t *xhci);
xhci_intrptr_t *xhci_setup_interrupter(xhci_dev_t *xhci, uint8_t n);
void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint);
xhci_cap_t *xhci_get_cap(xhci_dev_t *xhci, xhci_cap_t *cap_ptr, uint8_t cap_id);
xhci_protocol_t *xhci_get_protocols(xhci_dev_t *xhci);

xhci_port_t *xhci_discover_ports(xhci_dev_t *xhci);
int xhci_enable_port(xhci_dev_t *xhci, xhci_port_t *port);

void *xhci_run_command(xhci_dev_t *xhci, xhci_trb_t *trb);
int xhci_enable_slot(xhci_dev_t *xhci);
int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device);
int xhci_configure_endpoint(xhci_dev_t *xhci, xhci_device_t *device);
int xhci_evaluate_context(xhci_dev_t *xhci, xhci_device_t *device);

bool xhci_is_valid_event(xhci_intrptr_t *intr);

xhci_device_t *xhci_alloc_device(xhci_dev_t *xhci, xhci_port_t *port, uint8_t slot);
int xhci_ring_device_db(xhci_device_t *device);
void xhci_get_device_info(xhci_device_t *device);

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t *setup, uint8_t type);
int xhci_queue_data(xhci_device_t *device, uintptr_t buffer, uint16_t size, bool dir);
int xhci_queue_status(xhci_device_t *device, bool dir);
void *xhci_get_descriptor(xhci_device_t *device, uint8_t type, uint8_t index, size_t *size);
char *xhci_get_string_descriptor(xhci_device_t *device, uint8_t index);

xhci_ring_t *xhci_alloc_ring();
void xhci_free_ring(xhci_ring_t *ring);
void xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb);
void xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t **result);

#endif
