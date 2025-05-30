//
// Created by Aaron Gill-Braun on 2024-11-12.
//

#ifndef DRIVERS_USB_HOST_XHCI_H
#define DRIVERS_USB_HOST_XHCI_H

#include <kernel/base.h>
#include <kernel/queue.h>

#include <kernel/irq.h>
#include <kernel/sem.h>
#include <kernel/chan.h>

#include <kernel/usb/usb.h>
#include <kernel/bus/pci_v2.h>

typedef union xhci_trb xhci_trb_t;
struct xhci_input_ctrl_ctx;
struct xhci_slot_ctx;
struct xhci_endpoint_ctx;

#define ep_index(num, dir) ((num) + max((num) - 1, 0) + (dir))
#define ep_number(idx) (((idx) - ((idx) % 2 == 0) + 1) / 2)

#define MAX_ENDPOINTS 31

typedef struct bitmap bitmap_t;
typedef struct xhci_controller xhci_controller_t;
typedef struct xhci_device xhci_device_t;


typedef struct xhci_ring {
  union xhci_trb *base;   // ring base
  uint32_t index;         // ring enqueue/dequeue index
  uint32_t max_index;     // max index
  int cycle;              // cycle state
  sem_t events;           // event semaphore
} xhci_ring_t;

typedef struct xhci_protocol {
  uint8_t rev_major;   // major usb revision
  uint8_t rev_minor;   // minor usb revision
  uint8_t port_offset; // compatible port offset
  uint8_t port_count;  // compatible port count
  uint8_t slot_type;   // slot type to use with enable slot
  LIST_ENTRY(struct xhci_protocol) list;
} xhci_protocol_t;

// input context
typedef struct xhci_ictx {
  // pointers to the context structs
  struct xhci_input_ctrl_ctx *ctrl;
  struct xhci_slot_ctx *slot;
  struct xhci_endpoint_ctx *endpoint[31];
  void *buffer;
} xhci_ictx_t;

// device context
typedef struct xhci_dctx {
  struct xhci_slot_ctx *slot;
  struct xhci_endpoint_ctx *endpoint[31];
  void *buffer;
} xhci_dctx_t;

typedef struct xhci_port {
  uint8_t number;             // port number
  uint16_t speed;             // port speed
  xhci_protocol_t *protocol;  // port protocol
  xhci_device_t *device;      // attached device
  LIST_ENTRY(struct xhci_port) list;
} xhci_port_t;

typedef struct xhci_interrupter {
  uint8_t index;              // interrupter number
  uint8_t vector;             // mapped interrupt vector
  uintptr_t erst;             // event ring segment table
  xhci_ring_t *ring;          // event ring
} xhci_interrupter_t;

typedef struct xhci_endpoint {
  usb_endpoint_t *usb_endpoint;
  xhci_controller_t *host;
  xhci_device_t *device;

  uint8_t type;               // endpoint type
  uint8_t number;             // endpoint number
  uint8_t index;              // endpoint index
  struct xhci_endpoint_ctx *ctx;   // endpoint context

  xhci_ring_t *xfer_ring;     // transfer ring
  chan_t *xfer_ch;            // transfer channel
} xhci_endpoint_t;

typedef struct xhci_device {
  usb_device_t *usb_device;
  xhci_controller_t *host;
  xhci_port_t *port;

  uint8_t slot_id;            // device slot
  xhci_ictx_t *ictx;          // input context
  xhci_dctx_t *dctx;          // device context

  xhci_ring_t *evt_ring;      // device event ring
  xhci_interrupter_t *interrupter;

  mtx_t lock;
  cond_t event;

  xhci_endpoint_t *endpoints[MAX_ENDPOINTS];
  LIST_ENTRY(struct xhci_device) list;
} xhci_device_t;

typedef struct xhci_controller {
  pci_device_t *pci_dev;
  pid_t pid; // pid of the controller thread

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
  xhci_protocol_t *protocols;
  xhci_port_t *ports;
  xhci_device_t *devices;

  xhci_ring_t *cmd_ring;   // host command ring
  xhci_ring_t *evt_ring;   // host event ring

  chan_t *cmd_compl_ch;
  chan_t *xfer_evt_ch;
  chan_t *port_sts_ch;

  mtx_t lock;
  LIST_ENTRY(struct xhci_controller) list;
} xhci_controller_t;


// MARK: Public API

void register_xhci_controller(pci_device_t *device);

int xhci_usb_host_init(usb_host_t *usb_host);
int xhci_usb_host_start(usb_host_t *usb_host);
int xhci_usb_host_stop(usb_host_t *usb_host);
int xhci_usb_host_discover(usb_host_t *usb_host);

int xhci_usb_device_init(usb_device_t *usb_dev);
int xhci_usb_device_deinit(usb_device_t *usb_dev);
int xhci_usb_device_add_transfer(usb_device_t *usb_dev, usb_endpoint_t *endpoint, usb_transfer_t *transfer);
int xhci_usb_device_start_transfer(usb_device_t *usb_dev, usb_endpoint_t *endpoint);
int xhci_usb_device_await_event(usb_device_t *usb_dev, usb_endpoint_t *endpoint, usb_event_t *event);
int xhci_usb_device_read_descriptor(usb_device_t *usb_dev, usb_device_descriptor_t **out);

int xhci_usb_init_endpoint(usb_endpoint_t *usb_ep);
int xhci_usb_deinit_endpoint(usb_endpoint_t *usb_ep);

// MARK: Private API

int xhci_setup_controller(xhci_controller_t *host);
int xhci_reset_controller(xhci_controller_t *host);
int xhci_run_controller(xhci_controller_t *host);
int xhci_halt_controller(xhci_controller_t *host);

int xhci_enable_interrupter(xhci_controller_t *host, xhci_interrupter_t *intr);
int xhci_disable_interrupter(xhci_controller_t *host, xhci_interrupter_t *intr);
int xhci_setup_port(xhci_controller_t *host, xhci_port_t *port);
int xhci_enable_port(xhci_controller_t *host, xhci_port_t *port);

int xhci_setup_device(xhci_device_t *device);
int xhci_add_device_endpoint(xhci_endpoint_t *ep);
xhci_endpoint_t *xhci_get_device_endpoint(xhci_device_t *device, usb_dir_t direction);

int xhci_run_command_trb(xhci_controller_t *host, xhci_trb_t trb, xhci_trb_t *result);
int xhci_run_noop_cmd(xhci_controller_t *host);
int xhci_run_enable_slot_cmd(xhci_controller_t *host, xhci_port_t *port);
int xhci_run_address_device_cmd(xhci_controller_t *host, xhci_device_t *device);
int xhci_run_configure_ep_cmd(xhci_controller_t *host, xhci_device_t *device);
int xhci_run_evaluate_ctx_cmd(xhci_controller_t *host, xhci_device_t *device);

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t setup, uint8_t type);
int xhci_queue_data(xhci_device_t *device, uintptr_t buffer, uint16_t length, usb_dir_t direction);
int xhci_queue_status(xhci_device_t *device, usb_dir_t direction, bool ioc);
int xhci_queue_transfer(xhci_device_t *device, xhci_endpoint_t *ep, uintptr_t buffer, uint16_t length, bool ioc);
int xhci_ring_start_transfer(xhci_device_t *device, xhci_endpoint_t *ep);
int xhci_await_transfer(xhci_device_t *device, xhci_endpoint_t *ep, xhci_trb_t *result);

xhci_controller_t *xhci_alloc_controller(pci_device_t *pci_dev, pci_bar_t *bar);
xhci_protocol_t *xhci_alloc_protocols(xhci_controller_t *host);
xhci_port_t *xhci_alloc_ports(xhci_controller_t *host);

xhci_interrupter_t *xhci_alloc_interrupter(xhci_controller_t *host, irq_handler_t fn, void *data);
int xhci_free_interrupter(xhci_interrupter_t *intr);
xhci_device_t *xhci_alloc_device(xhci_controller_t *host, xhci_port_t *port, uint8_t slot_id);
int xhci_free_device(xhci_device_t *device);
xhci_endpoint_t *xhci_alloc_endpoint(xhci_device_t *device, uint8_t number, uint8_t type);
int xhci_free_endpoint(xhci_endpoint_t *ep);
xhci_ictx_t *xhci_alloc_input_ctx(xhci_device_t *device);
int xhci_free_input_ctx(xhci_ictx_t *ictx);
xhci_dctx_t *xhci_alloc_device_ctx(xhci_device_t *device);
int xhci_free_device_ctx(xhci_dctx_t *dctx);

xhci_ring_t *xhci_alloc_ring(size_t capacity);
void xhci_free_ring(xhci_ring_t *ring);
int xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t trb);
bool xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t *out);
uint64_t xhci_ring_device_ptr(xhci_ring_t *ring);
size_t xhci_ring_size(xhci_ring_t *ring);

#endif
