//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#ifndef KERNEL_USB_XHCI_H
#define KERNEL_USB_XHCI_H

#include <base.h>
#include <usb/usb.h>
#include <usb/xhci_hw.h>

#include <bus/pcie.h>

#include <irq.h>
#include <mutex.h>

#define ep_index(num, dir) ((num) + max((num) - 1, 0) + dir)
#define ep_number(idx) (((idx) - ((idx) % 2 == 0) + 1) / 2)

// Transfer Flags
#define XHCI_XFER_IOC 0x1 // interrupt on completion
#define XHCI_XFER_ISP 0x2 // interrupt on short packet
#define XHCI_XFER_NS  0x4 // no snoop


typedef struct xhci_dev xhci_dev_t;
typedef struct xhci_port xhci_port_t;

typedef struct xhci_ring {
  xhci_trb_t *ptr;    // ring base
  page_t *page;       // ring pages
  uint16_t index;     // ring enqueue/dequeue index
  uint16_t rd_index;  // ring read index
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

typedef struct xhci_speed {
  struct xhci_speed *next;
} xhci_speed_t;

typedef struct {
  uint8_t number;    // interrupter number
  uint8_t vector;    // mapped interrupt vector
  uintptr_t erst;    // event ring segment table
  xhci_ring_t *ring; // event ring
} xhci_intr_t;

typedef struct xhci_ep {
  uint8_t number;             // endpoint number
  uint8_t index;              // endpoint index
  uint8_t type;               // endpoint type
  uint8_t dir;                // endpoint direction
  xhci_endpoint_ctx_t *ctx;   // endpoint context
  xhci_ring_t *ring;          // transfer ring
  cond_t event;               // event condition
  usb_event_t last_event;     // last endpoint event
  struct xhci_ep *next;
} xhci_ep_t;

typedef struct xhci_device {
  uint8_t slot_id;
  uint8_t dev_class;
  uint8_t dev_subclass;
  uint8_t dev_protocol;
  uint8_t dev_config;
  uint8_t dev_if;

  xhci_dev_t *xhci;
  xhci_port_t *port;

  xhci_ring_t *ring;
  xhci_input_ctx_t *ictx;
  xhci_device_ctx_t *dctx;
  page_t *pages;

  usb_device_descriptor_t *desc;
  usb_config_descriptor_t **configs;
  xhci_ep_t *endpoints;
  xhci_intr_t *intr;

  thread_t *thread;
  cond_t event;
  cond_t event_ack;
} xhci_device_t;

typedef struct xhci_port {
  uint8_t number;            // port number
  uint8_t speed;             // port speed
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
  xhci_speed_t *speeds;
  xhci_port_t *ports;

  xhci_intr_t *intr;
  xhci_ring_t *cmd_ring;
} xhci_dev_t;


void xhci_init();
void xhci_setup_devices();

int xhci_init_controller(xhci_dev_t *xhci);
xhci_intr_t *xhci_setup_interrupter(xhci_dev_t *xhci, irq_handler_t fn, void *data);
void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint);
xhci_cap_t *xhci_get_cap(xhci_dev_t *xhci, xhci_cap_t *cap_ptr, uint8_t cap_id);
xhci_protocol_t *xhci_get_protocols(xhci_dev_t *xhci);
xhci_speed_t *xhci_get_speeds(xhci_dev_t *xhci);

xhci_port_t *xhci_discover_ports(xhci_dev_t *xhci);
int xhci_enable_port(xhci_dev_t *xhci, xhci_port_t *port);

void *xhci_run_command(xhci_dev_t *xhci, xhci_trb_t *trb);
int xhci_enable_slot(xhci_dev_t *xhci);
int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device);
int xhci_configure_endpoint(xhci_dev_t *xhci, xhci_device_t *device);
int xhci_evaluate_context(xhci_dev_t *xhci, xhci_device_t *device);

void *xhci_wait_for_transfer(xhci_device_t *device);
bool xhci_has_dequeue_event(xhci_intr_t *intr);
bool xhci_has_event(xhci_intr_t *intr);

xhci_device_t *xhci_alloc_device(xhci_dev_t *xhci, xhci_port_t *port, uint8_t slot);
xhci_ep_t *xhci_alloc_device_ep(xhci_device_t *device, usb_ep_descriptor_t *desc);
int xhci_get_device_configs(xhci_device_t *device);
int xhci_select_device_config(xhci_device_t *device);
xhci_ep_t *xhci_get_endpoint(xhci_device_t *device, uint8_t ep_num);
xhci_ep_t *xhci_find_endpoint(xhci_device_t *device, bool dir);
int xhci_ring_device_db(xhci_device_t *device);

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t *setup, uint8_t type);
int xhci_queue_data(xhci_device_t *device, uintptr_t buffer, uint16_t size, bool dir);
int xhci_queue_status(xhci_device_t *device, bool dir);
int xhci_queue_transfer(xhci_device_t *device, uintptr_t buffer, uint16_t size, bool dir, uint8_t flags);
void *xhci_get_descriptor(xhci_device_t *device, uint8_t type, uint8_t index, size_t *size);
char *xhci_get_string_descriptor(xhci_device_t *device, uint8_t index);

xhci_ring_t *xhci_alloc_ring();
void xhci_free_ring(xhci_ring_t *ring);
void xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb);
void xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t **result);
void xhci_ring_read_trb(xhci_ring_t *ring, xhci_trb_t **result);

#endif
