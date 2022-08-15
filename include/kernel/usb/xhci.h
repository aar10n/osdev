//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#ifndef KERNEL_USB_XHCI_H
#define KERNEL_USB_XHCI_H

#include <base.h>
#include <queue.h>

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


typedef struct xhci_controller xhci_controller_t;
typedef struct _xhci_device _xhci_device_t;
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

typedef struct _xhci_ring {
  xhci_trb_t *ptr;    // ring base
  page_t *page;       // ring pages
  uint32_t index;     // ring enqueue/dequeue index
  uint32_t max_index; // max index
  int cycle;          // cycle state
  cond_t cond;        // condition
} _xhci_ring_t;

typedef struct _xhci_protocol {
  uint8_t rev_major;   // major usb revision
  uint8_t rev_minor;   // minor usb revision
  uint8_t port_offset; // compatible port offset
  uint8_t port_count;  // compatible port count
  uint8_t slot_type;   // slot type to use with enable slot
  LIST_ENTRY(struct _xhci_protocol) list;
} _xhci_protocol_t;

// input context
typedef struct xhci_ictx {
  // pointers to the context structs
  xhci_input_ctrl_ctx_t *ctrl;
  xhci_slot_ctx_t *slot;
  xhci_endpoint_ctx_t *endpoint[31];

  _xhci_ring_t *ctrl_ring;
  page_t *pages;
} xhci_ictx_t;

// device context
typedef struct xhci_dctx {
  xhci_slot_ctx_t *slot;
  xhci_endpoint_ctx_t *endpoint[31];

  page_t *pages;
} xhci_dctx_t;

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

typedef struct xhci_interrupter {
  uint8_t index;        // interrupter number
  uint8_t vector;       // mapped interrupt vector
  uintptr_t erst;       // event ring segment table
  _xhci_ring_t *ring;   // event ring
  LIST_ENTRY(struct xhci_interrupter) list;
} xhci_interrupter_t;

typedef struct xhci_endpoint {
  uint8_t number;             // endpoint number
  uint8_t index;              // endpoint index
  uint8_t type;               // endpoint type
  uint8_t dir;                // endpoint direction
  xhci_endpoint_ctx_t *ctx;   // endpoint context
  _xhci_ring_t *ring;         // transfer ring
  usb_event_t last_event;     // last endpoint event
  LIST_ENTRY(struct xhci_endpoint) list;
} xhci_endpoint_t;

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

typedef struct _xhci_port {
  uint8_t number;            // port number
  uint8_t speed;             // port speed
  _xhci_protocol_t *protocol; // port protocol
  _xhci_device_t *device;     // attached device
  LIST_ENTRY(struct _xhci_port) list;
} _xhci_port_t;

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

//

typedef struct _xhci_device {
  xhci_controller_t *host;
  _xhci_port_t *port;

  uint8_t slot_id;
  uint8_t dev_class;
  uint8_t dev_subclass;
  uint8_t dev_protocol;
  uint8_t dev_config;
  uint8_t dev_if;

  xhci_ictx_t *ictx;  // input context
  xhci_dctx_t *dctx;  // device context

  usb_device_descriptor_t *desc;
  usb_config_descriptor_t *configs;
  xhci_endpoint_t *endpoints;
  xhci_interrupter_t *interrupters;

  _xhci_ring_t *xfer_ring;  // device transfer ring
  _xhci_ring_t *evt_ring;   // device event ring
  xhci_trb_t xfer_evt_trb;  // transfer event trb
  cond_t xfer_evt_cond;     // transfer event condition

  mutex_t lock;
  thread_t *thread;
  cond_t event;

  LIST_ENTRY(struct _xhci_device) list;
} _xhci_device_t;

typedef struct xhci_controller {
  pcie_device_t *pcie_device;

  uintptr_t phys_addr;
  uintptr_t address;

  // register offsets
  uintptr_t cap_base;
  uintptr_t op_base;
  uintptr_t rt_base;
  uintptr_t db_base;
  uintptr_t xcap_base;

  uintptr_t *dcbaap;
  uint8_t max_intr;
  xhci_interrupter_t *interrupters;
  _xhci_protocol_t *protocols;
  _xhci_port_t *ports;
  _xhci_device_t *devices;

  _xhci_ring_t *cmd_ring;   // host command ring
  _xhci_ring_t *evt_ring;   // host event ring
  xhci_trb_t cmd_compl_trb; // cmd compl event trb handoff
  cond_t cmd_compl_cond;    // cmd completion condition
  xhci_trb_t xfer_trb;      // transfer event trb handoff
  cond_t xfer_cond;         // transfer event condition

  mutex_t lock;
  thread_t *thread;

  LIST_ENTRY(struct xhci_controller) list;
} xhci_controller_t;


void _xhci_init(pcie_device_t *device);
void xhci_init();
int _xhci_setup_devices(xhci_controller_t *hc);
void xhci_setup_devices();

int _xhci_init_controller(xhci_controller_t *hc);
int _xhci_reset_controller(xhci_controller_t *hc);
int _xhci_start_controller(xhci_controller_t *hc);
int _xhci_halt_controller(xhci_controller_t *hc);

xhci_interrupter_t *_xhci_setup_interrupter(xhci_controller_t *hc, irq_handler_t fn, void *data);
_xhci_port_t *_xhci_discover_ports(xhci_controller_t *hc);
int _xhci_enable_port(xhci_controller_t *hc, _xhci_port_t *port);
void *_xhci_get_cap(xhci_controller_t *hc, uint8_t cap_id, void *last_cap);
_xhci_protocol_t *_xhci_get_protocols(xhci_controller_t *hc);

_xhci_device_t *_xhci_setup_device(xhci_controller_t *hc, _xhci_port_t *port, uint8_t slot);
xhci_endpoint_t *_xhci_setup_device_ep(_xhci_device_t *device, usb_ep_descriptor_t *desc);
usb_config_descriptor_t *_xhci_get_device_configs(_xhci_device_t *device);
int _xhci_select_device_config(_xhci_device_t *device);
xhci_endpoint_t *_xhci_get_device_ep(_xhci_device_t *device, uint8_t ep_num);
xhci_endpoint_t *_xhci_find_device_ep(_xhci_device_t *device, bool direction);
void *_xhci_get_device_descriptor(_xhci_device_t *device, uint8_t type, uint8_t index, size_t *bufsize);
char *_xhci_get_string_descriptor(_xhci_device_t *device, uint8_t index);

int _xhci_run_command_trb(xhci_controller_t *hc, xhci_trb_t trb, xhci_trb_t *result);
int _xhci_run_noop_cmd(xhci_controller_t *hc);
int _xhci_run_enable_slot_cmd(xhci_controller_t *hc, _xhci_port_t *port);
int _xhci_run_address_device_cmd(xhci_controller_t *hc, _xhci_device_t *device);
int _xhci_run_configure_ep_cmd(xhci_controller_t *hc, _xhci_device_t *device);
int _xhci_run_evaluate_ctx_cmd(xhci_controller_t *hc, _xhci_device_t *device);

int _xhci_queue_setup(_xhci_device_t *device, usb_setup_packet_t setup, uint8_t type);
int _xhci_queue_data(_xhci_device_t *device, uintptr_t buffer, uint16_t size, bool direction);
int _xhci_queue_status(_xhci_device_t *device, bool direction);
int _xhci_queue_transfer(_xhci_device_t *device, uintptr_t buffer, uint16_t size, bool direction, uint8_t flags);
int _xhci_await_transfer(_xhci_device_t *device, xhci_trb_t *result);

xhci_ictx_t *_xhci_alloc_input_ctx(_xhci_device_t *device);
xhci_dctx_t *_xhci_alloc_device_ctx(_xhci_device_t *device);

_xhci_ring_t *_xhci_alloc_ring(size_t capacity);
void _xhci_free_ring(_xhci_ring_t *ring);
int _xhci_ring_enqueue_trb(_xhci_ring_t *ring, xhci_trb_t trb);
bool _xhci_ring_dequeue_trb(_xhci_ring_t *ring, xhci_trb_t *out);
uint64_t _xhci_ring_device_ptr(_xhci_ring_t *ring);
size_t _xhci_ring_size(_xhci_ring_t *ring);

//

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
