//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#include <kernel/usb/xhci.h>
#include <kernel/usb/xhci_hw.h>

#include <kernel/usb/usb.h>
#include <kernel/bus/pcie.h>
#include <kernel/cpu/io.h>

#include <kernel/mm.h>
#include <kernel/irq.h>
#include <kernel/sched.h>
#include <kernel/mutex.h>
#include <kernel/clock.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#include <bitmap.h>

#define host_to_hc(host) ((xhci_controller_t *)((host)->data))
#define device_to_hd(device) ((_xhci_device_t *)((device)->host_data))
#define device_to_hc(device) ((xhci_controller_t *)((device)->host->data))

#define CMD_RING_SIZE   256
#define EVT_RING_SIZE   256
#define XFER_RING_SIZE  256
#define ERST_SIZE       1

#define QDEBUG(v) outdw(0x888, v)

static int num_controllers = 0;
static LIST_HEAD(xhci_controller_t) controllers;

usb_host_impl_t xhci_host_impl = {
  .init = xhci_host_init,
  .start = xhci_host_start,
  .stop = xhci_host_stop,
  .discover = xhci_host_discover,
};

usb_device_impl_t xhci_device_impl = {
  .init = xhci_device_init,
  .deinit = xhci_device_deinit,
  .add_transfer = xhci_add_transfer,
  .start_transfer = xhci_start_transfer,
  .await_event = xhci_await_event,
  .read_device_descriptor = xhci_read_device_descriptor,

  .init_endpoint = xhci_init_endpoint,
  .deinit_endpoint = xhci_deinit_endpoint,
};

static const char *xhci_ep_type_names[] = {
  [XHCI_ISOCH_OUT_EP] = "XHCI_ISOCH_OUT_EP",
  [XHCI_BULK_OUT_EP] = "XHCI_BULK_OUT_EP",
  [XHCI_INTR_OUT_EP] = "XHCI_INTR_OUT_EP",
  [XHCI_CTRL_BI_EP] = "XHCI_CTRL_BI_EP",
  [XHCI_ISOCH_IN_EP] = "XHCI_ISOCH_IN_EP",
  [XHCI_BULK_IN_EP] = "XHCI_BULK_IN_EP",
  [XHCI_INTR_IN_EP] = "XHCI_INTR_IN_EP",
};

static inline int get_ep_ctx_index(uint8_t ep_number, uint8_t ep_type) {
  // 0 - default control 0
  // 1 - ep context 1 OUT
  // 2 - ep context 1 IN
  // 3 - ep context 2 OUT
  // 4 - ep context 2 IN
  // 5 - ep context 3 OUT
  // 6 - ep context 3 IN

  switch (ep_type) {
    case XHCI_CTRL_BI_EP:
      return 0;
    case XHCI_ISOCH_OUT_EP:
    case XHCI_BULK_OUT_EP:
    case XHCI_INTR_OUT_EP:
      return (ep_number * 2) - 1;
    case XHCI_ISOCH_IN_EP:
    case XHCI_BULK_IN_EP:
    case XHCI_INTR_IN_EP:
      return ep_number * 2;
    default:
      unreachable;
  }
}

static inline uint8_t get_xhci_ep_type(usb_ep_type_t ep_type, usb_dir_t ep_dir) {
  switch (ep_type) {
    case USB_CONTROL_EP: return XHCI_CTRL_BI_EP;
    case USB_ISOCHRONOUS_EP: return ep_dir == USB_IN ? XHCI_ISOCH_IN_EP : XHCI_ISOCH_OUT_EP;
    case USB_BULK_EP: return ep_dir == USB_IN ? XHCI_BULK_IN_EP : XHCI_BULK_OUT_EP;
    case USB_INTERRUPT_EP: return ep_dir == USB_IN ? XHCI_INTR_IN_EP : XHCI_INTR_OUT_EP;
    default: unreachable;
  }
}

static inline const char *get_speed_str(uint16_t speed) {
  switch (speed) {
    case XHCI_FULL_SPEED:
      return "Full-speed (12 Mb/s)";
    case XHCI_LOW_SPEED:
      return "Low-speed (1.5 Mb/s)";
    case XHCI_HIGH_SPEED:
      return "High-speed (480 Mb/s)";
    case XHCI_SUPER_SPEED_G1X1:
      return "SuperSpeed Gen1 x1 (5 Gb/s)";
    case XHCI_SUPER_SPEED_G2X1:
      return "SuperSpeedPlus Gen2 x1 (10 Gb/s)";
    case XHCI_SUPER_SPEED_G1X2:
      return "SuperSpeedPlus Gen1 x2 (5 Gb/s)";
    case XHCI_SUPER_SPEED_G2X2:
      return "SuperSpeedPlus Gen2 x2 (10 Gb/s)";
    default:
      return "Unknown";
  }
}

static inline const char *get_revision_str(_xhci_protocol_t *protocol) {
  if (protocol->rev_major == XHCI_REV_MAJOR_2) {
    return "USB 2.0";
  } else if (protocol->rev_major == XHCI_REV_MAJOR_3) {
    if (protocol->rev_minor == XHCI_REV_MINOR_0) {
      return "USB 3.0";
    } else if (protocol->rev_minor == XHCI_REV_MINOR_1) {
      return "USB 3.1";
    } else if (protocol->rev_minor == XHCI_REV_MINOR_2) {
      return "USB 3.2";
    }
    return "USB 3.x";
  }
  return "USB ?";
}

static inline uint16_t get_default_ep0_packet_size(_xhci_port_t *port) {
  switch (port->speed) {
    case XHCI_LOW_SPEED:
    case XHCI_FULL_SPEED:
      return 8;
    case XHCI_HIGH_SPEED:
      return 64;
    case XHCI_SUPER_SPEED_G1X1:
    case XHCI_SUPER_SPEED_G2X1:
    case XHCI_SUPER_SPEED_G1X2:
    case XHCI_SUPER_SPEED_G2X2:
      return 512;
    default:
      // should never happen
      return 1;
  }
}

static inline void *get_capability_pointer(xhci_controller_t *hc, uint8_t cap_id, void *last_cap) {
  uint32_t *cap_ptr = last_cap;
  if (last_cap == NULL) {
    cap_ptr = (void *) hc->xcap_base;
  } else if (XCAP_NEXT(*cap_ptr) == 0) {
    return NULL;
  } else {
    cap_ptr = offset_ptr(cap_ptr, XCAP_NEXT(*cap_ptr));
  }

  while (true) {
    if (XCAP_ID(*cap_ptr) == cap_id) {
      return cap_ptr;
    }

    uintptr_t next = XCAP_NEXT(*cap_ptr);
    if (next == 0) {
      return NULL;
    }
    cap_ptr = offset_ptr(cap_ptr, next);
  }
}

static inline bool is_64_byte_context(xhci_controller_t *hc) {
  return HCCPARAMS1_CSZ(read32(hc->cap_base, XHCI_CAP_HCCPARAMS1));
}

static inline bool port_is_usb3(_xhci_port_t *port) {
  return port->protocol->rev_major == XHCI_REV_MAJOR_3;
}

static inline bool device_is_usb3(_xhci_device_t *device) {
  return device->port->protocol->rev_major == XHCI_REV_MAJOR_3;
}

//
// MARK:
//

void xhci_host_irq_handler(uint8_t vector, void *data) {
  xhci_controller_t *hc = data;
  // kprintf("[CPU#%d] xhci: >>> controller interrupt <<<\n", PERCPU_ID);
  uint32_t usbsts = read32(hc->op_base, XHCI_OP_USBSTS);

  // clear interrupt flag
  usbsts |= USBSTS_EVT_INT;
  write32(hc->op_base, XHCI_OP_USBSTS, usbsts);
  // clear interrupt pending flag
  uint32_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(0));
  iman |= IMAN_IP;
  write32(hc->rt_base, XHCI_INTR_IMAN(0), iman);

  if (usbsts & USBSTS_HC_ERR) {
    kprintf("xhci: >>>>> HOST CONTROLLER ERROR <<<<<<\n");
    _xhci_halt_controller(hc);
    return;
  } else if (usbsts & USBSTS_HS_ERR) {
    kprintf("xhci: >>>>> HOST SYSTEM ERROR <<<<<\n");
    return;
  }

  cond_signal(&hc->evt_ring->cond);
}

void xhci_device_irq_handler(uint8_t vector, void *data) {
  _xhci_device_t *device = data;
  xhci_controller_t *hc = device->host;
  uint8_t n = device->interrupter->index;
  // kprintf("[CPU#%d] xhci: >>> device interrupt <<<\n", PERCPU_ID);

  // clear interrupt flag
  uint32_t usbsts = read32(hc->op_base, XHCI_OP_USBSTS);
  usbsts |= USBSTS_EVT_INT;
  write32(hc->op_base, XHCI_OP_USBSTS, usbsts);
  // clear interrupt pending flag
  uint32_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(n));
  iman |= IMAN_IP;
  write32(hc->rt_base, XHCI_INTR_IMAN(n), iman);
  cond_signal(&device->evt_ring->cond);
}

//

int _xhci_handle_controller_event(xhci_controller_t *hc, xhci_trb_t trb) {
  if (trb.trb_type == TRB_TRANSFER_EVT) {
    // kprintf("xhci: transfer complete\n");
    chan_send(hc->xfer_evt_ch, trb.qword1);
  } else if (trb.trb_type == TRB_CMD_CMPL_EVT) {
    // kprintf("xhci: >> command completed <<\n");
    chan_send(hc->cmd_compl_ch, trb.qword1);
  } else if (trb.trb_type == TRB_PORT_STS_EVT) {
    chan_send(hc->port_sts_ch, trb.qword1);

    xhci_port_status_evt_trb_t port_trb = downcast_trb(&trb, xhci_port_status_evt_trb_t);
    _xhci_port_t *port = RLIST_FIND(p, hc->ports, list, (p->number == port_trb.port_id));
    if (port == NULL) {
      kprintf("xhci: port not initialized\n");
      return 0;
    } else {
      // kprintf("xhci: handling event [type = %d]\n", trb.trb_type);
    }

    uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1));
    port->speed = PORTSC_SPEED(portsc);
    // write32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1), portsc);
    // kprintf("xhci: >> event on port %d [ccs = %d]\n", port_trb.port_id, (portsc & PORTSC_CCS) != 0);

    _xhci_device_t *device = RLIST_FIND(d, hc->devices, list, d->port->number == port_trb.port_id);
    if (device != NULL && (portsc & PORTSC_CCS) != 0) {
      return 0;
    }

    // TODO: notify usb stack of device
  }
  return 0;
}

noreturn void *_xhci_controller_event_loop(void *arg) {
  xhci_controller_t *hc = arg;
  kprintf("[CPU#%d] xhci: starting controller event loop\n", PERCPU_ID);

  while (true) {
    cond_wait(&hc->evt_ring->cond);
    // kprintf("[CPU#%d] xhci: controller event\n", PERCPU_ID);

    uint64_t old_erdp = _xhci_ring_device_ptr(hc->evt_ring);
    xhci_trb_t trb;
    while (_xhci_ring_dequeue_trb(hc->evt_ring, &trb)) {
      if (trb.trb_type == TRB_PORT_STS_EVT) {
        xhci_port_status_evt_trb_t port_trb = downcast_trb(&trb, xhci_port_status_evt_trb_t);

        // uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1));
        // int ccs = (portsc & PORTSC_CCS) != 0;
        // int ped = (portsc & PORTSC_EN) != 0;
        // int csc = (portsc & PORTSC_CSC) != 0;
        // int pec = (portsc & PORTSC_PEC) != 0;
        // int prc = (portsc & PORTSC_PRC) != 0;
        // kprintf("xhci: >> event on port %d <<\n", port_trb.port_id);
        // kprintf("      ccs = %d | ped = %d\n", ccs, ped);
        // kprintf("      csc = %d | pec = %d | prc = %d\n", csc, pec, prc);

        // write32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1), portsc);
        // kprintf("xhci: >> event on port %d [ccs = %d]\n", port_trb.port_id, (portsc & PORTSC_CCS) != 0);
      } else if (_xhci_handle_controller_event(hc, trb) < 0) {
        kprintf("xhci: failed to handle event\n");
        _xhci_halt_controller(hc);
        thread_block();
        unreachable;
      }
    }

    uint64_t new_erdp = _xhci_ring_device_ptr(hc->evt_ring);
    uint64_t erdp = read64(hc->rt_base, XHCI_INTR_ERDP(0));
    if (old_erdp != new_erdp) {
      erdp &= ERDP_MASK;
      erdp |= ERDP_PTR(new_erdp) ;
    }
    // clear event handler busy flag
    erdp |= ERDP_EH_BUSY;
    write64(hc->rt_base, XHCI_INTR_ERDP(0), erdp);
  }
}


noreturn void *_xhci_device_event_loop(void *arg) {
  _xhci_device_t *device = arg;
  xhci_controller_t *hc = device->host;
  uint8_t n = device->interrupter->index;

  while (true) {
    cond_wait(&device->evt_ring->cond);
    // kprintf("[CPU#%d] xhci: device event\n", PERCPU_ID);

    // handler transfer event
    uint64_t old_erdp = _xhci_ring_device_ptr(device->evt_ring);
    xhci_trb_t trb;
    while (_xhci_ring_dequeue_trb(device->evt_ring, &trb)) {
      xhci_transfer_evt_trb_t xfer_trb = downcast_trb(&trb, xhci_transfer_evt_trb_t);
      // kprintf("dequeued -> trb %d | ep = %d [cc = %d, remaining = %u]\n",
      //         trb.trb_type, xfer_trb.endp_id, xfer_trb.compl_code, xfer_trb.trs_length);
      kassert(trb.trb_type == TRB_TRANSFER_EVT);

      uint8_t ep_index = xfer_trb.endp_id - 1;
      xhci_endpoint_t *ep = device->endpoints[ep_index];
      chan_send(device->endpoints[ep_index]->xfer_ch, trb.qword1);

      if (ep->usb_endpoint != NULL && ep->usb_endpoint->event_ch != NULL) {
        // form usb event
        usb_endpoint_t *usb_ep = ep->usb_endpoint;

        usb_event_t usb_event;
        if (ep->number == 0) {
          usb_event.type = USB_CTRL_EV; // control event
        } else {
          usb_event.type = usb_ep->dir == USB_IN ? USB_IN_EV : USB_OUT_EV; // data transfer event
        }

        if (xfer_trb.compl_code == CC_SUCCESS || xfer_trb.compl_code == CC_SHORT_PACKET) {
          usb_event.status = USB_SUCCESS;
        } else {
          usb_event.status = USB_ERROR;
        }

        uint64_t event_raw = *((uint64_t *) &usb_event);
        // kprintf("xhci event: %s | %s [CPU#%d]\n",
        //         usb_get_event_type_string(usb_event.type),
        //         usb_get_status_string(usb_event.status),
        //         PERCPU_ID);
        chan_send(usb_ep->event_ch, event_raw);
      }
    }

    uint64_t new_erdp = _xhci_ring_device_ptr(device->evt_ring);
    uint64_t erdp = read64(hc->rt_base, XHCI_INTR_ERDP(n));
    erdp &= ERDP_MASK;
    if (old_erdp != new_erdp) {
      erdp |= ERDP_PTR(new_erdp) ;
    }
    // clear event handler busy flag
    erdp |= ERDP_EH_BUSY;
    write64(hc->rt_base, XHCI_INTR_ERDP(n), erdp);
  }
}

// MARK: PCI Interface

void register_xhci_controller(pcie_device_t *device) {
  kassert(device->class_code == PCI_SERIAL_BUS_CONTROLLER);
  kassert(device->subclass == PCI_USB_CONTROLLER);
  kassert(device->prog_if == USB_PROG_IF_XHCI);

  pcie_bar_t *bar = SLIST_FIND(b, device->bars, next, b->kind == 0);
  if (bar == NULL) {
    kprintf("xhci: failed to register controller: no bars found\n");
    return;
  }

  xhci_controller_t *controller = NULL;
  LIST_FOREACH(controller, &controllers, list) {
    // why are duplicate devices showing up while enumerating the pci buses?
    // this is needed as a workaround to prevent double-registering xhci
    // controllers.
    if (controller->phys_addr == bar->phys_addr) {
      // skip duplicate controllers
      return;
    }
  }

  // map the xhci into the virtual memory space
  bar->virt_addr = (uintptr_t) vm_alloc_map_phys(bar->phys_addr, 0, align(bar->size, PAGE_SIZE), 0, PG_NOCACHE | PG_WRITE, "xhci");

  if (!HCCPARAMS1_AC64(read32(bar->virt_addr, XHCI_CAP_HCCPARAMS1))) {
    // we dont support 32-bit controllers right now
    kprintf("xhci: controller not supported (32-bit only)\n");
    // TODO: properly clean up controller resources
    return;
  }

  uint16_t version = CAP_VERSION(read32(bar->virt_addr, XHCI_CAP_LENGTH));
  uint8_t version_maj = (version >> 8) & 0xFF;
  uint8_t version_min = version & 0xFF;

  // allocate the xhci controller struct
  xhci_controller_t *hc = _xhci_alloc_controller(device, bar);
  LIST_ADD(&controllers, hc, list);
  num_controllers++;
  kprintf("xhci: registering controller %d\n", num_controllers);

  // initialize the usb host struct
  usb_host_t *host = kmalloc(sizeof(usb_host_t));
  memset(host, 0, sizeof(usb_host_t));
  host->name = kasprintf("xHCI Controller v%x.%x", version_maj, version_min);
  host->pci_device = device;
  host->host_impl = &xhci_host_impl;
  host->device_impl = &xhci_device_impl;
  host->data = hc;
  host->root = NULL;
  usb_register_host(host);
}

//
// MARK: USB Controller Interface
//

int xhci_host_init(usb_host_t *host) {
  xhci_controller_t *hc = host_to_hc(host);

  // reset controller to starting state
  if (_xhci_reset_controller(hc) < 0) {
    kprintf("xhci: failed to reset controller\n");
    _xhci_halt_controller(hc);
    return -1;
  }

  // then setup the controller
  if (_xhci_setup_controller(hc) < 0) {
    kprintf("xhci: failed to setup controller\n");
    return -1;
  }
  return 0;
}

int xhci_host_start(usb_host_t *host) {
  xhci_controller_t *hc = host_to_hc(host);

  // run controller
  if (_xhci_run_controller(hc) < 0) {
    kprintf("xhci: failed to start controller\n");
    return -1;
  }
  return 0;
}

int xhci_host_stop(usb_host_t *host) {
  xhci_controller_t *hc = host_to_hc(host);

  // halt controller
  if (_xhci_halt_controller(hc) < 0) {
    kprintf("xhci: failed to stop controller\n");
    return -1;
  }
  return 0;
}

int xhci_host_discover(usb_host_t *host) {
  xhci_controller_t *hc = host_to_hc(host);

  _xhci_port_t *port;
  RLIST_FOREACH(port, hc->ports, list) {
    uint8_t n = port->number - 1;
    uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(n));
    if (portsc & PORTSC_CCS) {
      kprintf("xhci: device connected to port %d\n", port->number);
      if (usb_handle_device_connect(host, port) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

//
// MARK: USB Device Interface
//

int xhci_device_init(usb_device_t *device) {
  xhci_controller_t *hc = device_to_hc(device);
  _xhci_port_t *port = device->host_data;

  // enable port
  if (_xhci_enable_port(hc, port) < 0) {
    kprintf("xhci: failed to enable port %d\n", port->number);
    return -1;
  }

  // enable slot to use with the device
  int slot_id = _xhci_run_enable_slot_cmd(hc, port);
  if (slot_id < 0) {
    kprintf("xhci: failed to enable slot for port %d\n", port->number);
    return -1;
  }

  _xhci_device_t *dev = _xhci_alloc_device(hc, port, slot_id);
  kassert(dev != NULL);
  dev->usb_device = device;
  dev->thread = thread_create(_xhci_device_event_loop, dev);
  thread_yield();

  dev->endpoints[0] = _xhci_alloc_endpoint(dev, 0, XHCI_CTRL_BI_EP);
  dev->endpoints[0]->ctx->max_packt_sz = get_default_ep0_packet_size(dev->port);
  dev->ictx->slot->intrptr_target = dev->interrupter->index;

  // setup the device
  if (_xhci_setup_device(dev) < 0) {
    kprintf("xhci: failed to setup device on port %d\n", port->number);
    _xhci_free_device(dev);
    return -1;
  }

  port->device = dev;
  device->host_data = dev;
  return 0;
}

int xhci_device_deinit(usb_device_t *device) {
  // TODO: tear down things properly
  xhci_controller_t *hc = device_to_hc(device);
  _xhci_device_t *dev = device_to_hd(device);
  _xhci_free_device(dev);
  kfree(dev);
  return 0;
}

int xhci_add_transfer(usb_device_t *device, usb_endpoint_t *endpoint, usb_transfer_t *transfer) {
  _xhci_device_t *dev = device_to_hd(device);
  xhci_endpoint_t *ep = endpoint->host_data;
  kassert(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  if (transfer->type == USB_SETUP_XFER) {
    usb_setup_packet_t packet = transfer->setup;

    // setup control transfer
    if (transfer->buffer == 0) {
      // no data stage
      _xhci_queue_setup(dev, packet, SETUP_DATA_NONE);
      _xhci_queue_status(dev, USB_OUT, true);
    } else {
      // has data stage
      bool is_pkt_in = packet.request_type.direction == USB_SETUP_DEV_TO_HOST;
      usb_dir_t dir = is_pkt_in ? USB_IN : USB_OUT;
      uint8_t type = is_pkt_in ?  SETUP_DATA_IN : SETUP_DATA_OUT;

      _xhci_queue_setup(dev, packet, type);
      _xhci_queue_data(dev, transfer->buffer, transfer->length, dir);
      _xhci_queue_status(dev, USB_OUT, false);
    }
  } else {
    // data transfer

    // a usb transfer with the USB_XFER_PART flag set are intended to
    // be followed by more transfers so only interrupt on the last one.
    bool ioc = (transfer->flags & USB_XFER_PART) == 0;

    // TODO: length usize -> u16 overflow check/auto split into multiple xfers
    kassert(transfer->length <= UINT16_MAX);
    _xhci_queue_transfer(dev, ep, transfer->buffer, transfer->length, ioc);
  }

  return 0;
}

int xhci_start_transfer(usb_device_t *device, usb_endpoint_t *endpoint) {
  _xhci_device_t *dev = device_to_hd(device);
  xhci_endpoint_t *ep = endpoint->host_data;
  kassert(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  if (_xhci_start_transfer(dev, ep) < 0) {
    kprintf("xhci: failed to start transfer\n");
    return -1;
  }

  return 0;
}

int xhci_await_event(usb_device_t *device, usb_endpoint_t *endpoint, usb_event_t *event) {
  _xhci_device_t *dev = device_to_hd(device);
  xhci_endpoint_t *ep = endpoint->host_data;
  kassert(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  xhci_transfer_evt_trb_t result;
  if (_xhci_await_transfer(dev, ep, cast_trb_ptr(&result)) < 0) {
    kprintf("xhci: failed to wait for transfer\n");
    return -1;
  }

  if (endpoint->number == 0) {
    // default control endpoint
    event->type = USB_CTRL_EV;
  } else {
    // data endpoint
    event->type = endpoint->dir == USB_IN ? USB_IN_EV : USB_OUT_EV;
  }

  if (result.compl_code == CC_SUCCESS || result.compl_code == CC_SHORT_PACKET) {
    event->status = USB_SUCCESS;
  } else {
    event->status = USB_ERROR;
    kprintf("xhci_await_event() | USB ERROR %d\n", result.compl_code);
  }
  return 0;
}

int xhci_read_device_descriptor(usb_device_t *device, usb_device_descriptor_t **out) {
  xhci_controller_t *hc = device_to_hc(device);
  _xhci_device_t *dev = device_to_hd(device);
  if (dev->port->speed == XHCI_FULL_SPEED) {
    // for FS devices, we should initially read the first 8 bytes
    // to determine ep0 max packet size. then update ep0 config and
    // evaluate context before reading rest of device descriptor.
    //
    // for all other devices the max packet size for the default control
    // endpoint will always be fixed for a given speed.
    usb_setup_packet_t get_desc0 = GET_DESCRIPTOR(DEVICE_DESCRIPTOR, 0, 8);
    usb_device_descriptor_t *temp = kmalloc(8);
    memset(temp, 0, 8);

    _xhci_queue_setup(dev, get_desc0, SETUP_DATA_IN);
    _xhci_queue_data(dev, kheap_ptr_to_phys(temp), 8, DATA_IN);
    _xhci_queue_status(dev, DATA_OUT, false);
    if (_xhci_start_transfer(dev, dev->endpoints[0]) < 0) {
      kprintf("xhci: failed to initiate transfer for device descriptor\n");
      return -1;
    }

    xhci_transfer_evt_trb_t result;
    if (_xhci_await_transfer(dev, dev->endpoints[0], cast_trb_ptr(&result)) < 0) {
      kprintf("xhci: failed to get device descriptor\n");
      kfree(temp);
      return -1;
    }

    // kprintf("xhci: device descriptor (0)\n");
    // kprintf("      length = %d | usb version = %x\n", temp->length, temp->usb_ver);
    // kprintf("      max packet sz (ep0) = %d\n", temp->max_packt_sz0);

    // update default control ep max packet size
    dev->ictx->ctrl->add_flags |= 1; // set A1 bit to 1
    dev->ictx->endpoint[0]->max_packt_sz = temp->max_packt_sz0;

    // evaluate context
    if (_xhci_run_evaluate_ctx_cmd(hc, dev) < 0) {
      kprintf("xhci: failed to evaluate context\n");
      kfree(temp);
      return -1;
    }
    // kprintf("xhci: context evaluated\n");
    kfree(temp);
  }

  // read full descriptor
  size_t size = sizeof(usb_device_descriptor_t);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(DEVICE_DESCRIPTOR, 0, size);
  usb_device_descriptor_t *desc = kmalloc(size);
  memset(desc, 0, size);

  _xhci_queue_setup(dev, get_desc, SETUP_DATA_IN);
  _xhci_queue_data(dev, kheap_ptr_to_phys(desc), size, DATA_IN);
  _xhci_queue_status(dev, DATA_OUT, false);

  if (_xhci_start_transfer(dev, dev->endpoints[0]) < 0) {
    kprintf("xhci: failed to initiate transfer for device descriptor\n");
    return -1;
  }

  xhci_transfer_evt_trb_t result;
  if (_xhci_await_transfer(dev, dev->endpoints[0], cast_trb_ptr(&result)) < 0) {
    kprintf("xhci: failed to get device descriptor\n");
    kfree(desc);
    return -1;
  }

  kassert(out != NULL);
  *out = desc;
  return 0;
}

//

int xhci_init_endpoint(usb_endpoint_t *endpoint) {
  xhci_controller_t *hc = device_to_hc(endpoint->device);
  _xhci_device_t *dev = device_to_hd(endpoint->device);
  xhci_ictx_t *ictx = dev->ictx;
  if (endpoint->number == 0) {
    // special default control endpoint
    endpoint->host_data = dev->endpoints[0];
    dev->endpoints[0]->usb_endpoint = endpoint;
    return 0;
  }

  uint8_t ep_num = endpoint->number;
  uint8_t ep_type = get_xhci_ep_type(endpoint->type, endpoint->dir);
  // kprintf("xhci: initializing endpoint | index = %d, number = %d, type = %d (%s)\n",
  //         get_ep_ctx_index(ep_num, ep_type), ep_num, ep_type, xhci_ep_type_names[ep_type]);

  xhci_endpoint_t *ep = _xhci_alloc_endpoint(dev, ep_num, ep_type);
  xhci_endpoint_ctx_t *ctx = ep->ctx;
  ctx->ep_type = ep_type;
  ctx->max_packt_sz = endpoint->max_pckt_sz;
  ctx->interval = endpoint->interval;
  ctx->max_burst_sz = 1;
  ctx->avg_trb_length = 8;
  ctx->max_streams = 0;
  ctx->mult = 0;
  ctx->cerr = 0;

  ictx->slot->ctx_entries++;
  ictx->ctrl->drop_flags = 0;
  ictx->ctrl->add_flags = 1 | (1 << (ep->index + 1));
  if (_xhci_run_configure_ep_cmd(hc, dev) < 0) {
    kprintf("xhci: failed to add endpoint\n");
    _xhci_free_endpoint(ep);
    return -1;
  }

  dev->endpoints[ep->index] = ep;
  endpoint->host_data = ep;
  ep->usb_endpoint = endpoint;
  return 0;
}

int xhci_deinit_endpoint(usb_endpoint_t *endpoint) {
  xhci_controller_t *hc = device_to_hc(endpoint->device);
  _xhci_device_t *dev = device_to_hd(endpoint->device);
  xhci_endpoint_t *ep = endpoint->host_data;
  xhci_ictx_t *ictx = dev->ictx;

  ictx->slot->ctx_entries--;
  ictx->ctrl->drop_flags = (1 << (ep->index + 1));
  ictx->ctrl->add_flags = 1;
  if (_xhci_run_configure_ep_cmd(hc, dev) < 0) {
    kprintf("xhci: failed to drop endpoint\n");
    // TODO: what to do here?
    ictx->slot->ctx_entries = dev->dctx->slot->ctx_entries;
    return -1;
  }

  _xhci_free_endpoint(ep);
  endpoint->host_data = NULL;
  return 0;
}

//
// MARK: Controller
//

int _xhci_setup_controller(xhci_controller_t *hc) {
  // configure the max slots enabled
  uint32_t max_slots = CAP_MAX_SLOTS(read32(hc->cap_base, XHCI_CAP_HCSPARAMS1));
  write32(hc->op_base, XHCI_OP_CONFIG, CONFIG_MAX_SLOTS_EN(max_slots));

  // setup device context base array pointer
  uint64_t dcbaap_ptr = virt_to_phys(hc->dcbaap);
  write64(hc->op_base, XHCI_OP_DCBAAP, DCBAAP_PTR(dcbaap_ptr));

  // set up the command ring
  uint64_t crcr = CRCR_PTR(_xhci_ring_device_ptr(hc->cmd_ring));
  if (hc->cmd_ring->cycle) {
    crcr |= CRCR_RCS;
  }
  write64(hc->op_base, XHCI_OP_CRCR, crcr);
  return 0;
}

int _xhci_reset_controller(xhci_controller_t *hc) {
  kprintf("xhci: resetting controller\n");
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  usbcmd &= ~USBCMD_RUN;
  usbcmd |= USBCMD_HC_RESET;
  write32(hc->op_base, XHCI_OP_USBCMD, usbcmd);

  // TODO: use better timeout
  uint64_t timeout = UINT16_MAX;
  while ((read32(hc->op_base, XHCI_OP_USBSTS) & USBSTS_NOT_READY) != 0) {
    timeout--;
    if (timeout == 0) {
      kprintf("xhci: timed out while resetting controller\n");
      return -1;
    }

    cpu_pause();
    thread_yield();
  }

  kprintf("xhci: controller reset\n");
  return 0;
}

int _xhci_run_controller(xhci_controller_t *hc) {
  // enable root interrupter
  if (_xhci_enable_interrupter(hc, hc->interrupter) < 0) {
    return -1;
  }

  // run the controller
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  usbcmd |= USBCMD_RUN | USBCMD_INT_EN | USBCMD_HS_ERR_EN;
  write32(hc->op_base, XHCI_OP_USBCMD, usbcmd);

  uint64_t timeout = UINT16_MAX;
  while ((read32(hc->op_base, XHCI_OP_USBSTS) & USBSTS_HC_HALTED) != 0) {
    timeout--;
    if (timeout == 0) {
      kprintf("xhci: timed out while starting controller\n");
      return -1;
    }

    cpu_pause();
    thread_yield();
  }

  // test out the command ring
  // kprintf("xhci: testing no-op command\n");
  if (_xhci_run_noop_cmd(hc) < 0) {
    kprintf("xhci: failed to execute no-op command\n");
    return -1;
  }

  return 0;
}

int _xhci_halt_controller(xhci_controller_t *hc) {
  // disable root interrupter
  if (_xhci_disable_interrupter(hc, hc->interrupter) < 0) {
    kprintf("xhci: failed to disable root interrupter\n");
  }

  // TODO: stop all endpoints
  // TODO: free all xhci resources

  // halt the command ring
  uint64_t crcr = read64(hc->op_base, XHCI_OP_CRCR);
  crcr |= CRCR_CA; // command abort
  write64(hc->op_base, XHCI_OP_CRCR, crcr);

  kprintf("xhci: stopping command ring\n");
  while ((read64(hc->op_base, XHCI_OP_CRCR) & CRCR_CRR) != 0) {
    cpu_pause();
  }

  // halt the controller
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  usbcmd &= ~USBCMD_RUN;       // clear run/stop bit
  usbcmd &= ~USBCMD_INT_EN;    // clear interrupt enable bit
  usbcmd &= ~USBCMD_HS_ERR_EN; // clear host system error enable bit
  write32(hc->op_base, XHCI_OP_USBCMD, usbcmd);

  kprintf("xhci: halting controller\n");
  while ((read64(hc->op_base, XHCI_OP_USBSTS) & USBSTS_HC_HALTED) != 0) {
    cpu_pause();
  }
  return 0;
}

//

int _xhci_enable_interrupter(xhci_controller_t *hc, xhci_interrupter_t *intr) {
  uint8_t n = intr->index;
  if (irq_enable_msi_interrupt(intr->vector, n, hc->pcie_device) < 0) {
    kprintf("xhci: failed to enable msi interrupt\n");
    return -1;
  }

  uintptr_t erstba_ptr = kheap_ptr_to_phys((void *) intr->erst);
  uintptr_t erdp_ptr = _xhci_ring_device_ptr(intr->ring);
  write32(hc->rt_base, XHCI_INTR_IMOD(n), IMOD_INTERVAL(4000));
  write32(hc->rt_base, XHCI_INTR_ERSTSZ(n), ERSTSZ(ERST_SIZE));
  write64(hc->rt_base, XHCI_INTR_ERSTBA(n), ERSTBA_PTR(erstba_ptr));
  write64(hc->rt_base, XHCI_INTR_ERDP(n), ERDP_PTR(erdp_ptr));

  uint32_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(n));
  iman |= IMAN_IE;
  write32(hc->rt_base, XHCI_INTR_IMAN(n), iman);
  return 0;
}

int _xhci_disable_interrupter(xhci_controller_t *hc, xhci_interrupter_t *intr) {
  uint8_t n = intr->index;
  if (irq_disable_msi_interrupt(intr->vector, n, hc->pcie_device) < 0) {
    kprintf("xhci: failed to disable msi interrupt\n");
    return -1;
  }

  uint32_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(n));
  iman &= ~IMAN_IE;
  write32(hc->rt_base, XHCI_INTR_IMAN(n), iman);
  return 0;
}

int _xhci_setup_port(xhci_controller_t *hc, _xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(n));
  // enable system events for device connects
  portsc |= PORTSC_WCE;
  // enable system events for device disconnects
  portsc |= PORTSC_WDE;
  // enable system events for over-current changes
  portsc |= PORTSC_WOE;
  write32(hc->op_base, XHCI_PORT_SC(n), portsc);
  return 0;
}

int _xhci_enable_port(xhci_controller_t *hc, _xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(n));

  if (port_is_usb3(port)) {
    // USB3
    // devices will automatically advance to the Enabled state
    // as part of the attatch process.
  } else {
    // USB2
    // devices need the port to be reset to advance the port to
    // the Enabled state. write '1' to the PORTSC PR bit.
    portsc &= PORTSC_MASK;
    portsc |= PORTSC_PRC;
    write32(hc->op_base, XHCI_PORT_SC(n), portsc);

    portsc = read32(hc->op_base, XHCI_PORT_SC(n)) & PORTSC_MASK;
    portsc |= PORTSC_RESET;
    write32(hc->op_base, XHCI_PORT_SC(n), portsc);

    while ((read32(hc->op_base, XHCI_PORT_SC(n)) & PORTSC_PRC) == 0) {
      cpu_pause();
    }

    portsc = read32(hc->op_base, XHCI_PORT_SC(n));
    portsc &= ~PORTSC_PRC;
  }

  if (!(portsc & PORTSC_EN)) {
    return -1;
  }
  return 0;
}

//
// MARK: Devices
//

int _xhci_setup_device(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  _xhci_port_t *port = device->port;

  if (_xhci_enable_interrupter(hc, device->interrupter) < 0) {
    kprintf("xhci: failed to enable interrupter for device on port %d\n", port->number);
    return -1;
  }

  // setup control endpoint
  xhci_endpoint_t *ep0 = device->endpoints[0];
  ep0->ctx->max_packt_sz = get_default_ep0_packet_size(device->port);
  device->ictx->slot->intrptr_target = device->interrupter->index;
  hc->dcbaap[device->slot_id] = virt_to_phys(device->dctx->buffer);

  // address device
  if (_xhci_run_address_device_cmd(hc, device) < 0) {
    kprintf("xhci: failed to address device\n");
    return -1;
  }

  // kprintf("xhci: device initialized on port %d\n", device->port->number);
  return 0;
}

int _xhci_add_device_endpoint(xhci_endpoint_t *ep) {
  xhci_controller_t *hc = ep->host;
  _xhci_device_t *device = ep->device;
  device->ictx->ctrl->add_flags = 1 | (1 << (ep->index + 1));
  device->ictx->ctrl->drop_flags = 0;

  if (_xhci_run_evaluate_ctx_cmd(hc, device) < 0) {
    kprintf("xhci: failed to evaluate context\n");
    return -1;
  }
  return 0;
}

xhci_endpoint_t *_xhci_get_device_endpoint(_xhci_device_t *device, usb_dir_t direction) {
  for (int i = 0; i < MAX_ENDPOINTS; i++) {
    xhci_endpoint_t *ep = device->endpoints[i];
    if (ep == NULL) {
      continue;
    }

    switch (ep->ctx->ep_type) {
      case XHCI_ISOCH_OUT_EP:
      case XHCI_BULK_OUT_EP:
      case XHCI_INTR_OUT_EP:
        if (direction == USB_OUT) {
          return ep;
        }
        continue;
      case XHCI_ISOCH_IN_EP:
      case XHCI_BULK_IN_EP:
      case XHCI_INTR_IN_EP:
        if (direction == USB_IN) {
          return ep;
        }
        continue;
      default:
        continue;
    }
  }

  return NULL;
}

//
// MARK: Commannds
//

int _xhci_run_command_trb(xhci_controller_t *hc, xhci_trb_t trb, xhci_trb_t *result) {
  // kprintf("xhci: running command [type = %d]\n", trb.trb_type);
  _xhci_ring_enqueue_trb(hc->cmd_ring, trb);

  // ring the host doorbell
  write32(hc->db_base, XHCI_DB(0), DB_TARGET(0));
  uint64_t data;
  if (chan_recv(hc->cmd_compl_ch, &data) < 0) {
    kprintf("xhci: failed to await command trb on channel\n");
    return -1;
  }

  // kprintf("xhci: event received\n");
  xhci_trb_t raw_trb = { .qword0 = 0, .qword1 = data };
  xhci_transfer_evt_trb_t evt_trb = downcast_trb(&raw_trb, xhci_transfer_evt_trb_t);
  if (result != NULL) {
    *result = cast_trb(&evt_trb);
  }
  return evt_trb.compl_code == CC_SUCCESS;
}

int _xhci_run_noop_cmd(xhci_controller_t *hc) {
  xhci_noop_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_NOOP_CMD;

  xhci_cmd_compl_evt_trb_t result;
  if (_xhci_run_command_trb(hc, cast_trb(&cmd), upcast_trb_ptr(&result)) < 0) {
    return -1;
  }
  return 0;
}

int _xhci_run_enable_slot_cmd(xhci_controller_t *hc, _xhci_port_t *port) {
  xhci_enabl_slot_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_ENABL_SLOT_CMD;
  cmd.slot_type = port->protocol->slot_type;

  xhci_cmd_compl_evt_trb_t result;
  if (_xhci_run_command_trb(hc, cast_trb(&cmd), upcast_trb_ptr(&result)) < 0) {
    return -1;
  }
  return result.slot_id;
}

int _xhci_run_address_device_cmd(xhci_controller_t *hc, _xhci_device_t *device) {
  // kprintf("xhci: addressing device\n");

  xhci_addr_dev_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_ADDR_DEV_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

int _xhci_run_configure_ep_cmd(xhci_controller_t *hc, _xhci_device_t *device) {
  // kprintf("xhci: configuring endpoint\n");
  xhci_config_ep_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_CONFIG_EP_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

int _xhci_run_evaluate_ctx_cmd(xhci_controller_t *hc, _xhci_device_t *device) {
  kprintf("xhci: evaluating context\n");

  xhci_eval_ctx_cmd_trb_t cmd;
  cmd.trb_type = TRB_EVAL_CTX_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

//
// MARK: Transfers
//

int _xhci_queue_setup(_xhci_device_t *device, usb_setup_packet_t setup, uint8_t type) {
  if (type != SETUP_DATA_NONE && type != SETUP_DATA_OUT && type != SETUP_DATA_IN) {
    return -EINVAL;
  }

  xhci_endpoint_t *ep = device->endpoints[0];

  xhci_setup_trb_t trb;
  clear_trb(&trb);
  memcpy(&trb, &setup, sizeof(usb_setup_packet_t));
  trb.trb_type = TRB_SETUP_STAGE;
  trb.trs_length = sizeof(usb_setup_packet_t); // 8
  trb.tns_type = type;
  trb.intr_trgt = device->interrupter->index;
  trb.idt = 1;
  trb.ioc = 0;

  _xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_data(_xhci_device_t *device, uintptr_t buffer, uint16_t length, usb_dir_t direction) {
  xhci_endpoint_t *ep = device->endpoints[0];

  xhci_data_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_DATA_STAGE;
  trb.buf_ptr = buffer;
  trb.trs_length = length;
  trb.td_size = 0;
  trb.intr_trgt = device->interrupter->index;
  trb.dir = direction;
  trb.isp = 0;
  trb.ioc = 1;

  _xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_status(_xhci_device_t *device, usb_dir_t direction, bool ioc) {
  xhci_endpoint_t *ep = device->endpoints[0];

  xhci_status_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_STATUS_STAGE;
  trb.intr_trgt = device->interrupter->index;
  trb.dir = direction;
  trb.ioc = ioc;

  _xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_transfer(_xhci_device_t *device, xhci_endpoint_t *ep, uintptr_t buffer, uint16_t length, bool ioc) {
  xhci_normal_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_NORMAL;
  trb.buf_ptr = buffer;
  trb.trs_length = length;
  trb.intr_trgt = device->interrupter->index;
  trb.isp = 0;
  trb.ioc = ioc;

  _xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_start_transfer(_xhci_device_t *device, xhci_endpoint_t *ep) {
  xhci_controller_t *hc = device->host;

  // ring the slot doorbell
  uint8_t target = ep->index + 1;
  write32(hc->db_base, XHCI_DB(device->slot_id), DB_TARGET(target));
  return 0;
}

int _xhci_await_transfer(_xhci_device_t *device, xhci_endpoint_t *ep, xhci_trb_t *result) {
  uint64_t data;
  if (chan_recv(ep->xfer_ch, &data) < 0) {
    kprintf("xhci: failed to await transfer on channel\n");
    return -1;
  }

  xhci_trb_t trb = { .qword0 = 0, .qword1 = data };
  xhci_transfer_evt_trb_t evt_trb = downcast_trb(&trb, xhci_transfer_evt_trb_t);
  kassert(evt_trb.trb_type == TRB_TRANSFER_EVT);
  if (result != NULL) {
    *result = cast_trb(&evt_trb);
  }
  return evt_trb.compl_code == CC_SUCCESS;
}

//
// MARK: Structures
//

xhci_controller_t *_xhci_alloc_controller(pcie_device_t *device, pcie_bar_t *bar) {
  kassert(bar->kind == 0);
  kassert(bar->phys_addr != 0);
  kassert(bar->virt_addr != 0);

  xhci_controller_t *hc = kmalloc(sizeof(xhci_controller_t));
  hc->pcie_device = device;
  hc->address = bar->virt_addr;
  hc->phys_addr = bar->phys_addr;

  hc->cap_base = hc->address;
  hc->op_base = hc->address + CAP_LENGTH(read32(hc->cap_base, XHCI_CAP_LENGTH));
  hc->db_base = hc->address + DBOFF_OFFSET(read32(hc->cap_base, XHCI_CAP_DBOFF));
  hc->rt_base = hc->address + RTSOFF_OFFSET(read32(hc->cap_base, XHCI_CAP_RTSOFF));
  hc->xcap_base = hc->address + HCCPARAMS1_XECP(read32(hc->cap_base, XHCI_CAP_HCCPARAMS1));

  hc->dcbaap = NULL;
  hc->intr_numbers = create_bitmap(CAP_MAX_INTRS(read32(hc->cap_base, XHCI_CAP_HCSPARAMS1)));
  hc->interrupter = _xhci_alloc_interrupter(hc, xhci_host_irq_handler, hc);
  hc->protocols = _xhci_alloc_protocols(hc);
  hc->ports = _xhci_alloc_ports(hc);
  hc->devices = NULL;

  hc->cmd_ring = _xhci_alloc_ring(CMD_RING_SIZE);
  hc->evt_ring = hc->interrupter->ring;

  hc->cmd_compl_ch = chan_alloc(EVT_RING_SIZE, 0);
  hc->xfer_evt_ch = chan_alloc(EVT_RING_SIZE, 0);
  hc->port_sts_ch = chan_alloc(EVT_RING_SIZE, 0);

  // allocate device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * CAP_MAX_SLOTS(read32(hc->cap_base, XHCI_CAP_HCSPARAMS1));
  void *dcbaap = kmalloca(dcbaap_size, 64);
  memset(dcbaap, 0, dcbaap_size);
  hc->dcbaap = dcbaap;

  // event thread
  mutex_init(&hc->lock, 0);
  hc->thread = thread_create(_xhci_controller_event_loop, hc);
  thread_yield();

  return hc;
}

_xhci_protocol_t *_xhci_alloc_protocols(xhci_controller_t *hc) {
  LIST_HEAD(_xhci_protocol_t) protocols = LIST_HEAD_INITR;

  uint32_t *cap = NULL;
  while (true) {
    cap = get_capability_pointer(hc, XHCI_CAP_PROTOCOL, cap);
    if (cap == NULL) {
      break;
    }

    uint8_t rev_minor = (cap[0] >> 16) & 0xFF;
    uint8_t rev_major = (cap[0] >> 24) & 0xFF;
    uint8_t port_offset = cap[2] & 0xFF;
    uint8_t port_count = (cap[2] >> 8) & 0xFF;
    uint8_t slot_type = cap[3] & 0x1F;

    if (rev_major == XHCI_REV_MAJOR_2) {
      kassert(rev_minor == XHCI_REV_MINOR_0);
    }

    kprintf("xhci: supported protocol 'USB %x.%x' (%d ports)\n", rev_major, rev_minor / 0x10, port_count);

    _xhci_protocol_t *protocol = kmalloc(sizeof(_xhci_protocol_t));
    protocol->rev_major = rev_major;
    protocol->rev_minor = rev_minor;
    protocol->port_offset = port_offset;
    protocol->port_count = port_count;
    protocol->slot_type = slot_type;
    LIST_ADD(&protocols, protocol, list);
  }

  return LIST_FIRST(&protocols);
}

_xhci_port_t *_xhci_alloc_ports(xhci_controller_t *hc) {
  LIST_HEAD(_xhci_port_t) ports = LIST_HEAD_INITR;

  _xhci_protocol_t *protocol = NULL;
  RLIST_FOREACH(protocol, hc->protocols, list) {
    uint8_t offset = protocol->port_offset;
    uint8_t count = protocol->port_count;
    for (int i = offset; i < offset + count; i++) {
      _xhci_port_t *port = kmalloc(sizeof(_xhci_port_t));
      port->number = i;
      port->protocol = protocol;
      port->speed = 0;
      port->device = NULL;
      LIST_ADD(&ports, port, list);
    }
  }

  return LIST_FIRST(&ports);
}

//

xhci_interrupter_t *_xhci_alloc_interrupter(xhci_controller_t *hc, irq_handler_t fn, void *data) {
  index_t n = bitmap_get_set_free(hc->intr_numbers);
  kassert(n >= 0);

  int irq = irq_alloc_software_irqnum();
  kassert(irq >= 0);
  irq_register_irq_handler(irq, fn, data);
  irq_enable_msi_interrupt(irq, n, hc->pcie_device);

  size_t erst_size = sizeof(xhci_erst_entry_t) * ERST_SIZE;
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  _xhci_ring_t *ring = _xhci_alloc_ring(EVT_RING_SIZE);
  erst[0].rs_addr = _xhci_ring_device_ptr(ring);
  erst[0].rs_size = _xhci_ring_size(ring);

  xhci_interrupter_t *intr = kmalloc(sizeof(xhci_interrupter_t));
  memset(intr, 0, sizeof(xhci_interrupter_t));
  intr->index = n;
  intr->vector = irq;
  intr->ring = ring;
  intr->erst = (uintptr_t) erst;
  return intr;
}

int _xhci_free_interrupter(xhci_interrupter_t *intr) {
  _xhci_free_ring(intr->ring);
  kfree((void *) intr->erst);
  // TODO: irq free vector
  kfree(intr);
  return 0;
}

_xhci_device_t *_xhci_alloc_device(xhci_controller_t *hc, _xhci_port_t *port, uint8_t slot_id) {
  _xhci_device_t *device = kmalloc(sizeof(_xhci_device_t));
  memset(device, 0, sizeof(_xhci_device_t));

  device->host = hc;
  device->port = port;

  device->slot_id = slot_id;
  device->ictx = _xhci_alloc_input_ctx(device);
  device->dctx = _xhci_alloc_device_ctx(device);

  device->interrupter = _xhci_alloc_interrupter(hc, xhci_device_irq_handler, device);
  device->evt_ring = device->interrupter->ring;
  LIST_ENTRY_INIT(&device->list);

  mutex_init(&device->lock, 0);
  cond_init(&device->event, 0);
  device->thread = NULL;
  return device;
}

int _xhci_free_device(_xhci_device_t *device) {
  _xhci_free_input_ctx(device->ictx);
  _xhci_free_device_ctx(device->dctx);
  _xhci_free_interrupter(device->interrupter);

  for (int i = 0; i < MAX_ENDPOINTS; i++) {
    xhci_endpoint_t *ep = device->endpoints[i];
    if (ep != NULL) {
      _xhci_free_endpoint(ep);
    }
  }

  // TODO: delete thread
  return 0;
}

xhci_endpoint_t *_xhci_alloc_endpoint(_xhci_device_t *device, uint8_t number, uint8_t type) {
  xhci_controller_t *hc = device->host;
  xhci_endpoint_t *ep = kmalloc(sizeof(xhci_endpoint_t));
  memset(ep, 0, sizeof(xhci_endpoint_t));
  ep->host = hc;
  ep->device = device;
  ep->number = number;
  ep->index = get_ep_ctx_index(number, type);
  ep->type = type;
  ep->ctx = device->ictx->endpoint[ep->index];
  ep->xfer_ring = _xhci_alloc_ring(XFER_RING_SIZE);
  ep->xfer_ch = chan_alloc(EVT_RING_SIZE, 0);

  ep->ctx->tr_dequeue_ptr = _xhci_ring_device_ptr(ep->xfer_ring) | ep->xfer_ring->cycle;
  return ep;
}

int _xhci_free_endpoint(xhci_endpoint_t *ep) {
  _xhci_free_ring(ep->xfer_ring);
  kfree(ep);
  return 0;
}


xhci_ictx_t *_xhci_alloc_input_ctx(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  void *ptr = vmalloc(PAGE_SIZE, PG_WRITE | PG_NOCACHE);

  xhci_ictx_t *ictx = kmallocz(sizeof(xhci_ictx_t));
  ictx->buffer = ptr;
  ictx->ctrl = ptr;
  ictx->slot = offset_ptr(ptr, ctxsz);
  for (int i = 0; i < MAX_ENDPOINTS; i++) {
    ictx->endpoint[i] = offset_ptr(ptr, ctxsz * (i + 2));
  }

  xhci_input_ctrl_ctx_t *ctrl_ctx = ictx->ctrl;
  ctrl_ctx->add_flags |= 0x3;

  xhci_slot_ctx_t *slot_ctx = ictx->slot;
  slot_ctx->root_hub_port = device->port->number;
  slot_ctx->route_string = 0;
  slot_ctx->speed = device->port->speed;
  slot_ctx->ctx_entries = 1;
  return ictx;
}

int _xhci_free_input_ctx(xhci_ictx_t *ictx) {
  vfree(ictx->buffer);
  kfree(ictx);
  return 0;
}

xhci_dctx_t *_xhci_alloc_device_ctx(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  xhci_dctx_t *dctx = kmallocz(sizeof(xhci_dctx_t));
  dctx->buffer = vmalloc(PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  dctx->slot = dctx->buffer;
  for (int i = 0; i < 31; i++) {
    dctx->endpoint[i] = offset_ptr(dctx->buffer, ctxsz * (i + 2));
  }

  return dctx;
}

int _xhci_free_device_ctx(xhci_dctx_t *dctx) {
  vfree(dctx->buffer);
  kfree(dctx);
  return 0;
}

//
// MARK: TRB Rings
//

_xhci_ring_t *_xhci_alloc_ring(size_t capacity) {
  size_t size = capacity * sizeof(xhci_trb_t);

  _xhci_ring_t *ring = kmallocz(sizeof(_xhci_ring_t));
  ring->base = vmalloc(capacity * sizeof(xhci_trb_t), PG_WRITE);
  ring->index = 0;
  ring->max_index = capacity;
  ring->cycle = 1;
  return ring;
}

void _xhci_free_ring(_xhci_ring_t *ring) {
  vfree(ring->base);
  kfree(ring);
}

int _xhci_ring_enqueue_trb(_xhci_ring_t *ring, xhci_trb_t trb) {
  kassert(trb.trb_type != 0);
  trb.cycle = ring->cycle;
  ring->base[ring->index] = trb;
  ring->index++;

  if (ring->index == ring->max_index - 1) {
    xhci_link_trb_t link;
    clear_trb(&link);
    link.trb_type = TRB_LINK;
    link.cycle = ring->cycle;
    link.toggle_cycle = 1;
    link.rs_addr = virt_to_phys(ring->base);
    ring->base[ring->index] = cast_trb(&link);

    ring->index = 0;
    ring->cycle = !ring->cycle;
  }
  return 0;
}

bool _xhci_ring_dequeue_trb(_xhci_ring_t *ring, xhci_trb_t *out) {
  kassert(out != NULL);
  xhci_trb_t trb = ring->base[ring->index];
  if (trb.trb_type == 0) {
    return false;
  }

  ring->index++;
  if (ring->index == ring->max_index) {
    ring->index = 0;
    ring->cycle = !ring->cycle;
  }
  *out = trb;
  return true;
}

uint64_t _xhci_ring_device_ptr(_xhci_ring_t *ring) {
  return virt_to_phys(ring->base) + (ring->index * sizeof(xhci_trb_t));
}

size_t _xhci_ring_size(_xhci_ring_t *ring) {
  return ring->max_index * sizeof(xhci_trb_t);
}

//
// MARK: Debugging
//

void _xhci_debug_host_registers(xhci_controller_t *hc) {
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  uint32_t usbsts = read32(hc->op_base, XHCI_OP_USBSTS);
  uint64_t crcr = read64_split(hc->op_base, XHCI_OP_CRCR);

  uint64_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(0));
  uint64_t imod = read32(hc->rt_base, XHCI_INTR_IMOD(0));
  uint64_t erdp = read64_split(hc->rt_base, XHCI_INTR_ERDP(0));

  kprintf("  usbcmd: %#034b\n", usbcmd);
  kprintf("  usbsts: %#034b\n", usbsts);
  kprintf("  crcr: %018p | %#06b\n", crcr & A64_MASK, crcr & 0xF);
  kprintf("  iman: %#04b\n", iman);
  kprintf("  imodc: %d | imodi: %d\n", IMOD_COUNTER(imod), IMOD_INTERVAL(imod));
  kprintf("  erdp: %018p | %#b\n", ERDP_PTR(erdp), erdp & ~ERDP_PTR(UINT64_MAX));
}

void _xhci_debug_port_registers(xhci_controller_t *hc, _xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(n));
  kprintf("  ccs: %d\n", (portsc & PORTSC_CCS) != 0);
  kprintf("  ped: %d\n", (portsc & PORTSC_EN) != 0);
  kprintf("  oca: %d\n", (portsc & PORTSC_OCA) != 0);
  kprintf("  pr: %d\n", (portsc & PORTSC_RESET) != 0);
  kprintf("  pls: %d\n", PORTSC_PLS(portsc));
  kprintf("  pp: %d\n", (portsc & PORTSC_POWER) != 0);
  kprintf("  speed: %d\n", PORTSC_SPEED(portsc));
  kprintf("  csc: %d\n", (portsc & PORTSC_CSC) != 0);
  kprintf("  pec: %d\n", (portsc & PORTSC_PEC) != 0);
  kprintf("  cas: %d\n", (portsc & PORTSC_CAS) != 0);
}

void _xhci_debug_device_context(_xhci_device_t *device) {
}
