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
#include <chan.h>

#define ep_index(num, dir) ((num) + max((num) - 1, 0) + dir)
#define ep_number(idx) (((idx) - ((idx) % 2 == 0) + 1) / 2)

#define MAX_ENDPOINTS 31

typedef struct bitmap bitmap_t;
typedef struct xhci_controller xhci_controller_t;
typedef struct _xhci_device _xhci_device_t;

typedef struct _xhci_ring {
  xhci_trb_t *base;   // ring base
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
  void *buffer;
} xhci_ictx_t;

// device context
typedef struct xhci_dctx {
  xhci_slot_ctx_t *slot;
  xhci_endpoint_ctx_t *endpoint[31];
  void *buffer;
} xhci_dctx_t;

typedef struct _xhci_port {
  uint8_t number;             // port number
  uint16_t speed;             // port speed
  _xhci_protocol_t *protocol; // port protocol
  _xhci_device_t *device;     // attached device
  LIST_ENTRY(struct _xhci_port) list;
} _xhci_port_t;

typedef struct xhci_interrupter {
  uint8_t index;        // interrupter number
  uint8_t vector;       // mapped interrupt vector
  uintptr_t erst;       // event ring segment table
  _xhci_ring_t *ring;   // event ring
} xhci_interrupter_t;

typedef struct xhci_endpoint {
  usb_endpoint_t *usb_endpoint;
  xhci_controller_t *host;
  _xhci_device_t *device;

  uint8_t type;             // endpoint type
  uint8_t number;           // endpoint number
  uint8_t index;            // endpoint index
  xhci_endpoint_ctx_t *ctx; // endpoint context

  _xhci_ring_t *xfer_ring;  // transfer ring
  chan_t *xfer_ch;          // transfer channel
} xhci_endpoint_t;

typedef struct _xhci_device {
  usb_device_t *usb_device;
  xhci_controller_t *host;
  _xhci_port_t *port;

  uint8_t slot_id;        // device slot
  xhci_ictx_t *ictx;      // input context
  xhci_dctx_t *dctx;      // device context

  _xhci_ring_t *evt_ring; // device event ring
  xhci_interrupter_t *interrupter;

  mutex_t lock;
  thread_t *thread;
  cond_t event;

  xhci_endpoint_t *endpoints[MAX_ENDPOINTS];
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

  uint64_t *dcbaap;
  bitmap_t *intr_numbers;
  xhci_interrupter_t *interrupter;
  _xhci_protocol_t *protocols;
  _xhci_port_t *ports;
  _xhci_device_t *devices;

  _xhci_ring_t *cmd_ring;   // host command ring
  _xhci_ring_t *evt_ring;   // host event ring

  chan_t *cmd_compl_ch;
  chan_t *xfer_evt_ch;
  chan_t *port_sts_ch;

  mutex_t lock;
  thread_t *thread;

  LIST_ENTRY(struct xhci_controller) list;
} xhci_controller_t;


// MARK: Public API

void register_xhci_controller(pcie_device_t *device);

int xhci_host_init(usb_host_t *host);
int xhci_host_start(usb_host_t *host);
int xhci_host_stop(usb_host_t *host);
int xhci_host_discover(usb_host_t *host);

int xhci_device_init(usb_device_t *device);
int xhci_device_deinit(usb_device_t *device);
int xhci_add_transfer(usb_device_t *device, usb_endpoint_t *endpoint, usb_transfer_t *transfer);
int xhci_start_transfer(usb_device_t *device, usb_endpoint_t *endpoint);
int xhci_await_event(usb_device_t *device, usb_endpoint_t *endpoint, usb_event_t *event);
// int xhci_queue_transfer(usb_device_t *device, usb_transfer_t *transfer);
// int xhci_await_transfer(usb_device_t *device, usb_transfer_t *transfer, usb_event_t *event);
int xhci_read_device_descriptor(usb_device_t *device, usb_device_descriptor_t **out);

int xhci_init_endpoint(usb_endpoint_t *endpoint);
int xhci_deinit_endpoint(usb_endpoint_t *endpoint);

// MARK: Private API

int _xhci_setup_controller(xhci_controller_t *hc);
int _xhci_reset_controller(xhci_controller_t *hc);
int _xhci_run_controller(xhci_controller_t *hc);
int _xhci_halt_controller(xhci_controller_t *hc);

int _xhci_enable_interrupter(xhci_controller_t *hc, xhci_interrupter_t *intr);
int _xhci_disable_interrupter(xhci_controller_t *hc, xhci_interrupter_t *intr);
int _xhci_setup_port(xhci_controller_t *hc, _xhci_port_t *port);
int _xhci_enable_port(xhci_controller_t *hc, _xhci_port_t *port);

int _xhci_setup_device(_xhci_device_t *device);
int _xhci_add_device_endpoint(xhci_endpoint_t *ep);
xhci_endpoint_t *_xhci_get_device_endpoint(_xhci_device_t *device, usb_dir_t direction);

int _xhci_run_command_trb(xhci_controller_t *hc, xhci_trb_t trb, xhci_trb_t *result);
int _xhci_run_noop_cmd(xhci_controller_t *hc);
int _xhci_run_enable_slot_cmd(xhci_controller_t *hc, _xhci_port_t *port);
int _xhci_run_address_device_cmd(xhci_controller_t *hc, _xhci_device_t *device);
int _xhci_run_configure_ep_cmd(xhci_controller_t *hc, _xhci_device_t *device);
int _xhci_run_evaluate_ctx_cmd(xhci_controller_t *hc, _xhci_device_t *device);

int _xhci_queue_setup(_xhci_device_t *device, usb_setup_packet_t setup, uint8_t type);
int _xhci_queue_data(_xhci_device_t *device, uintptr_t buffer, uint16_t length, usb_dir_t direction);
int _xhci_queue_status(_xhci_device_t *device, usb_dir_t direction, bool ioc);
int _xhci_queue_transfer(_xhci_device_t *device, xhci_endpoint_t *ep, uintptr_t buffer, uint16_t length, bool ioc);
int _xhci_start_transfer(_xhci_device_t *device, xhci_endpoint_t *ep);
int _xhci_await_transfer(_xhci_device_t *device, xhci_endpoint_t *ep, xhci_trb_t *result);

xhci_controller_t *_xhci_alloc_controller(pcie_device_t *device, pcie_bar_t *bar);
_xhci_protocol_t *_xhci_alloc_protocols(xhci_controller_t *hc);
_xhci_port_t *_xhci_alloc_ports(xhci_controller_t *hc);

xhci_interrupter_t *_xhci_alloc_interrupter(xhci_controller_t *hc, irq_handler_t fn, void *data);
int _xhci_free_interrupter(xhci_interrupter_t *intr);
_xhci_device_t *_xhci_alloc_device(xhci_controller_t *hc, _xhci_port_t *port, uint8_t slot_id);
int _xhci_free_device(_xhci_device_t *device);
xhci_endpoint_t *_xhci_alloc_endpoint(_xhci_device_t *device, uint8_t number, uint8_t type);
int _xhci_free_endpoint(xhci_endpoint_t *ep);
xhci_ictx_t *_xhci_alloc_input_ctx(_xhci_device_t *device);
int _xhci_free_input_ctx(xhci_ictx_t *ictx);
xhci_dctx_t *_xhci_alloc_device_ctx(_xhci_device_t *device);
int _xhci_free_device_ctx(xhci_dctx_t *dctx);

_xhci_ring_t *_xhci_alloc_ring(size_t capacity);
void _xhci_free_ring(_xhci_ring_t *ring);
int _xhci_ring_enqueue_trb(_xhci_ring_t *ring, xhci_trb_t trb);
bool _xhci_ring_dequeue_trb(_xhci_ring_t *ring, xhci_trb_t *out);
uint64_t _xhci_ring_device_ptr(_xhci_ring_t *ring);
size_t _xhci_ring_size(_xhci_ring_t *ring);

#endif
