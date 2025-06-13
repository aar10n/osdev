//
// Created by Aaron Gill-Braun on 2024-11-12.
//

#include "xhci.h"
#include "xhci_hw.h"

#include <kernel/usb/usb.h>
#include <kernel/cpu/io.h>

#include <kernel/mm.h>
#include <kernel/irq.h>
#include <kernel/device.h>
#include <kernel/sched.h>
#include <kernel/mutex.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#include <bitmap.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...)
// #define DPRINTF(fmt, ...) kprintf("xhci: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("xhci: %s: " fmt, __func__, ##__VA_ARGS__)

#define usb_host_to_host(host) ((xhci_controller_t *)((host)->data))
#define usb_dev_to_device(device) ((xhci_device_t *)((device)->host_data))
#define usb_dev_to_host(device) ((xhci_controller_t *)((device)->host->data))

#define CMD_RING_SIZE   256
#define EVT_RING_SIZE   256
#define XFER_RING_SIZE  256
#define ERST_SIZE       1

static LIST_HEAD(xhci_controller_t) hosts;
static int num_hosts = 0;

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

static inline uint16_t get_default_ep0_packet_size(xhci_port_t *port) {
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

static inline void *get_capability_pointer(xhci_controller_t *host, uint8_t cap_id, void *last_cap) {
  uint32_t *cap_ptr = last_cap;
  if (last_cap == NULL) {
    cap_ptr = (void *) host->xcap_base;
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

static inline const char *get_revision_str(xhci_protocol_t *protocol) {
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

static inline bool is_64_byte_context(xhci_controller_t *host) {
  return HCCPARAMS1_CSZ(read32(host->cap_base, XHCI_CAP_HCCPARAMS1));
}

static inline bool port_is_usb3(xhci_port_t *port) {
  return port->protocol->rev_major == XHCI_REV_MAJOR_3;
}

//
// MARK: Interrupt Handling
//

void xhci_host_irq_handler(struct trapframe *frame) {
  xhci_controller_t *host = (void *) frame->data;
  // DPRINTF(">>> controller interrupt <<<\n");
  uint32_t usbsts = read32(host->op_base, XHCI_OP_USBSTS);

  // clear interrupt flag
  usbsts |= USBSTS_EVT_INT;
  write32(host->op_base, XHCI_OP_USBSTS, usbsts);
  // clear interrupt pending flag
  uint32_t iman = read32(host->rt_base, XHCI_INTR_IMAN(0));
  iman |= IMAN_IP;
  write32(host->rt_base, XHCI_INTR_IMAN(0), iman);

  if (usbsts & USBSTS_HC_ERR) {
    EPRINTF(">>>>> HOST CONTROLLER ERROR <<<<<<\n");
    xhci_halt_controller(host);
    return;
  } else if (usbsts & USBSTS_HS_ERR) {
    EPRINTF(">>>>> HOST SYSTEM ERROR <<<<<\n");
    return;
  }

  sem_up(&host->evt_ring->events);
}

void xhci_device_irq_handler(struct trapframe *frame) {
  xhci_device_t *device = (void *) frame->data;
  xhci_controller_t *host = device->host;
  uint8_t n = device->interrupter->index;
  DPRINTF(">>> device interrupt <<<\n");

  // clear interrupt flag
  uint32_t usbsts = read32(host->op_base, XHCI_OP_USBSTS);
  usbsts |= USBSTS_EVT_INT;
  write32(host->op_base, XHCI_OP_USBSTS, usbsts);
  // clear interrupt pending flag
  uint32_t iman = read32(host->rt_base, XHCI_INTR_IMAN(n));
  iman |= IMAN_IP;
  write32(host->rt_base, XHCI_INTR_IMAN(n), iman);

  sem_up(&device->evt_ring->events);
}

//
// MARK: Event Loops
//

int xhci_handle_controller_event(xhci_controller_t *host, xhci_trb_t trb) {
  int res;
  if (trb.trb_type == TRB_TRANSFER_EVT) {
    DPRINTF("transfer complete\n");
    if ((res = chan_send(host->xfer_evt_ch, &trb)) < 0) {
      EPRINTF("failed to send transfer event [{:err}]\n", res);
      return res;
    }
  } else if (trb.trb_type == TRB_CMD_CMPL_EVT) {
    DPRINTF(">> command completed <<\n");
    if ((res = chan_send(host->cmd_compl_ch, &trb)) < 0) {
      EPRINTF("failed to send command completion event [{:err}]\n", res);
      return res;
    }
  } else if (trb.trb_type == TRB_PORT_STS_EVT) {
    if ((res = chan_send(host->port_sts_ch, &trb)) < 0) {
      EPRINTF("failed to send port status event [{:err}]\n", res);
      return res;
    }

    xhci_port_status_evt_trb_t port_trb = downcast_trb(&trb, xhci_port_status_evt_trb_t);
    xhci_port_t *port = RLIST_FIND(p, host->ports, list, (p->number == port_trb.port_id));
    if (port == NULL) {
      EPRINTF("port not initialized\n");
      return 0;
    } else {
      DPRINTF("handling event [type = %d]\n", trb.trb_type);
    }

    uint32_t portsc = read32(host->op_base, XHCI_PORT_SC(port_trb.port_id - 1));
    port->speed = PORTSC_SPEED(portsc);
    // write32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1), portsc);
    // DPRINTF(">> event on port %d [ccs = %d]\n", port_trb.port_id, (portsc & PORTSC_CCS) != 0);

    xhci_device_t *device = RLIST_FIND(d, host->devices, list, d->port->number == port_trb.port_id);
    if (device != NULL && (portsc & PORTSC_CCS) != 0) {
      return 0;
    }

    // TODO: notify usb stack of device
  }
  return 0;
}

int xhci_controller_event_loop(xhci_controller_t *host) {
  DPRINTF("starting controller event loop\n");

  while (true) {
    sem_down(&host->evt_ring->events);
    DPRINTF(">>> controller event <<<\n");

    uint64_t old_erdp = xhci_ring_device_ptr(host->evt_ring);
    xhci_trb_t trb;
    while (xhci_ring_dequeue_trb(host->evt_ring, &trb)) {
      if (trb.trb_type == TRB_PORT_STS_EVT) {
        xhci_port_status_evt_trb_t port_trb = downcast_trb(&trb, xhci_port_status_evt_trb_t);

        // uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1));
        // int ccs = (portsc & PORTSC_CCS) != 0;
        // int ped = (portsc & PORTSC_EN) != 0;
        // int csc = (portsc & PORTSC_CSC) != 0;
        // int pec = (portsc & PORTSC_PEC) != 0;
        // int prc = (portsc & PORTSC_PRC) != 0;
        // DPRINTF(">> event on port %d <<\n", port_trb.port_id);
        // DPRINTF("      ccs = %d | ped = %d\n", ccs, ped);
        // DPRINTF("      csc = %d | pec = %d | prc = %d\n", csc, pec, prc);

        // write32(hc->op_base, XHCI_PORT_SC(port_trb.port_id - 1), portsc);
        // kprintf("xhci: >> event on port %d [ccs = %d]\n", port_trb.port_id, (portsc & PORTSC_CCS) != 0);
      } else if (xhci_handle_controller_event(host, trb) < 0) {
        EPRINTF("failed to handle event\n");
        xhci_halt_controller(host);
        break;
      }
    }

    uint64_t new_erdp = xhci_ring_device_ptr(host->evt_ring);
    uint64_t erdp = read64(host->rt_base, XHCI_INTR_ERDP(0));
    if (old_erdp != new_erdp) {
      erdp &= ERDP_MASK;
      erdp |= ERDP_PTR(new_erdp) ;
    }
    // clear event handler busy flag
    erdp |= ERDP_EH_BUSY;
    write64(host->rt_base, XHCI_INTR_ERDP(0), erdp);
  }

  EPRINTF("exiting event loop\n");
  return -1;
}

int xhci_device_event_loop(xhci_device_t *device) {
  DPRINTF("starting device event loop\n");
  xhci_controller_t *host = device->host;
  uint8_t n = device->interrupter->index;

  while (true) {
    sem_down(&device->evt_ring->events);
    DPRINTF(">>> device event <<<\n");

    // handler transfer event
    uint64_t old_erdp = xhci_ring_device_ptr(device->evt_ring);
    xhci_transfer_evt_trb_t trb;
    while (xhci_ring_dequeue_trb(device->evt_ring, (void *) &trb)) {
      ASSERT(trb.trb_type == TRB_TRANSFER_EVT);
      DPRINTF("dequeued -> trb %d | ep = %d [cc = %d, remaining = %u]\n",
              trb.trb_type, trb.endp_id, trb.compl_code, trb.trs_length);

      uint8_t ep_index = trb.endp_id - 1;
      xhci_endpoint_t *ep = device->endpoints[ep_index];

      int res;
      if ((res = chan_send(device->endpoints[ep_index]->xfer_ch, &trb)) < 0) {
        EPRINTF("failed to send transfer event [{:err}]\n", res);
        xhci_halt_controller(host);
        break;
      }
      DPRINTF("sent transfer event to endpoint %d\n", ep_index);

      if (ep->usb_endpoint != NULL && ep->usb_endpoint->event_ch != NULL) {
        // form usb event
        usb_endpoint_t *usb_ep = ep->usb_endpoint;
        usb_event_t usb_event;
        if (ep->number == 0) {
          usb_event.type = USB_CTRL_EV; // control event
        } else {
          usb_event.type = usb_ep->dir == USB_IN ? USB_IN_EV : USB_OUT_EV; // data transfer event
        }

        if (trb.compl_code == CC_SUCCESS || trb.compl_code == CC_SHORT_PACKET) {
          usb_event.status = USB_SUCCESS;
        } else {
          usb_event.status = USB_ERROR;
        }

        DPRINTF("event: %s | %s\n",
                usb_get_event_type_string(usb_event.type),
                usb_get_status_string(usb_event.status));
        if ((res = chan_send(usb_ep->event_ch, &usb_event)) < 0) {
          EPRINTF("failed to send usb event [{:err}]\n", res);
          xhci_halt_controller(host);
          break;
        }
      }
    }

    uint64_t new_erdp = xhci_ring_device_ptr(device->evt_ring);
    uint64_t erdp = read64(host->rt_base, XHCI_INTR_ERDP(n));
    erdp &= ERDP_MASK;
    if (old_erdp != new_erdp) {
      erdp |= ERDP_PTR(new_erdp) ;
    }
    // clear event handler busy flag
    erdp |= ERDP_EH_BUSY;
    write64(host->rt_base, XHCI_INTR_ERDP(n), erdp);
  }

  EPRINTF("exiting event loop\n");
  todo(); // handle cleanup
  return -1;
}

//
// MARK: Controller
//

int xhci_setup_controller(xhci_controller_t *host) {
  // configure the max slots enabled
  uint32_t max_slots = CAP_MAX_SLOTS(read32(host->cap_base, XHCI_CAP_HCSPARAMS1));
  write32(host->op_base, XHCI_OP_CONFIG, CONFIG_MAX_SLOTS_EN(max_slots));

  // setup device context base array pointer
  uint64_t dcbaap_ptr = virt_to_phys(host->dcbaap);
  write64(host->op_base, XHCI_OP_DCBAAP, DCBAAP_PTR(dcbaap_ptr));

  // set up the command ring
  uint64_t crcr = CRCR_PTR(xhci_ring_device_ptr(host->cmd_ring));
  if (host->cmd_ring->cycle) {
    crcr |= CRCR_RCS;
  }
  write64(host->op_base, XHCI_OP_CRCR, crcr);
  return 0;
}

int xhci_reset_controller(xhci_controller_t *host) {
  DPRINTF("resetting controller\n");
  uint32_t usbcmd = read32(host->op_base, XHCI_OP_USBCMD);
  usbcmd &= ~USBCMD_RUN;
  usbcmd |= USBCMD_HC_RESET;
  write32(host->op_base, XHCI_OP_USBCMD, usbcmd);

  struct spin_delay delay = new_spin_delay(SHORT_DELAY, 10000);
  while ((read32(host->op_base, XHCI_OP_USBSTS) & USBSTS_NOT_READY) != 0) {
    if (!spin_delay_wait(&delay)) {
      EPRINTF("timed out while resetting controller\n");
      return -1;
    }
  }

  DPRINTF("controller reset\n");
  return 0;
}

int xhci_run_controller(xhci_controller_t *host) {
  // enable root interrupter
  if (xhci_enable_interrupter(host, host->interrupter) < 0) {
    return -1;
  }

  // run the controller
  uint32_t usbcmd = read32(host->op_base, XHCI_OP_USBCMD);
  usbcmd |= USBCMD_RUN | USBCMD_INT_EN | USBCMD_HS_ERR_EN;
  write32(host->op_base, XHCI_OP_USBCMD, usbcmd);

  struct spin_delay delay = new_spin_delay(SHORT_DELAY, 1000);
  while ((read32(host->op_base, XHCI_OP_USBSTS) & USBSTS_NOT_READY) != 0) {
    if (!spin_delay_wait(&delay)) {
      EPRINTF("timed out while starting controller\n");
      return -1;
    }
  }

  // test out the command ring
  // DPRINTF("testing no-op command\n");
  if (xhci_run_noop_cmd(host) < 0) {
    EPRINTF("failed to execute no-op command\n");
    return -1;
  }

  return 0;
}

int xhci_halt_controller(xhci_controller_t *host) {
  // disable root interrupter
  if (xhci_disable_interrupter(host, host->interrupter) < 0) {
    EPRINTF("failed to disable root interrupter\n");
  }

  // TODO: stop all endpoints
  // TODO: free all xhci resources

  // halt the command ring
  uint64_t crcr = read64(host->op_base, XHCI_OP_CRCR);
  crcr |= CRCR_CA; // command abort
  write64(host->op_base, XHCI_OP_CRCR, crcr);

  DPRINTF("stopping command ring\n");
  while ((read64(host->op_base, XHCI_OP_CRCR) & CRCR_CRR) != 0) {
    cpu_pause();
  }

  // halt the controller
  uint32_t usbcmd = read32(host->op_base, XHCI_OP_USBCMD);
  usbcmd &= ~USBCMD_RUN;       // clear run/stop bit
  usbcmd &= ~USBCMD_INT_EN;    // clear interrupt enable bit
  usbcmd &= ~USBCMD_HS_ERR_EN; // clear host system error enable bit
  write32(host->op_base, XHCI_OP_USBCMD, usbcmd);

  DPRINTF("halting controller\n");
  while ((read64(host->op_base, XHCI_OP_USBSTS) & USBSTS_HC_HALTED) != 0) {
    cpu_pause();
  }
  return 0;
}

//

int xhci_enable_interrupter(xhci_controller_t *host, xhci_interrupter_t *intr) {
  uint8_t n = intr->index;
  if (irq_enable_msi_interrupt(intr->vector, n, host->pci_dev) < 0) {
    EPRINTF("failed to enable msi interrupt\n");
    return -1;
  }

  uintptr_t erstba_ptr = kheap_ptr_to_phys((void *) intr->erst);
  uintptr_t erdp_ptr = xhci_ring_device_ptr(intr->ring);
  write32(host->rt_base, XHCI_INTR_IMOD(n), IMOD_INTERVAL(4000));
  write32(host->rt_base, XHCI_INTR_ERSTSZ(n), ERSTSZ(ERST_SIZE));
  write64(host->rt_base, XHCI_INTR_ERSTBA(n), ERSTBA_PTR(erstba_ptr));
  write64(host->rt_base, XHCI_INTR_ERDP(n), ERDP_PTR(erdp_ptr));

  uint32_t iman = read32(host->rt_base, XHCI_INTR_IMAN(n));
  iman |= IMAN_IE;
  write32(host->rt_base, XHCI_INTR_IMAN(n), iman);
  return 0;
}

int xhci_disable_interrupter(xhci_controller_t *host, xhci_interrupter_t *intr) {
  uint8_t n = intr->index;
  if (irq_disable_msi_interrupt(intr->vector, n, host->pci_dev) < 0) {
    EPRINTF("failed to disable msi interrupt\n");
    return -1;
  }

  uint32_t iman = read32(host->rt_base, XHCI_INTR_IMAN(n));
  iman &= ~IMAN_IE;
  write32(host->rt_base, XHCI_INTR_IMAN(n), iman);
  return 0;
}

int xhci_setup_port(xhci_controller_t *host, xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(host->op_base, XHCI_PORT_SC(n));
  // enable system events for device connects
  portsc |= PORTSC_WCE;
  // enable system events for device disconnects
  portsc |= PORTSC_WDE;
  // enable system events for over-current changes
  portsc |= PORTSC_WOE;
  write32(host->op_base, XHCI_PORT_SC(n), portsc);
  return 0;
}

int xhci_enable_port(xhci_controller_t *host, xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(host->op_base, XHCI_PORT_SC(n));

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
    write32(host->op_base, XHCI_PORT_SC(n), portsc);

    portsc = read32(host->op_base, XHCI_PORT_SC(n)) & PORTSC_MASK;
    portsc |= PORTSC_RESET;
    write32(host->op_base, XHCI_PORT_SC(n), portsc);

    // reset port timeout
    struct spin_delay delay = new_spin_delay(SHORT_DELAY, 10000);
    while ((read32(host->op_base, XHCI_PORT_SC(n)) & PORTSC_PRC) == 0) {
      if (!spin_delay_wait(&delay)) {
        EPRINTF("timed out while resetting port %d\n", n);
        return -1;
      }
    }

    portsc = read32(host->op_base, XHCI_PORT_SC(n));
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

int xhci_setup_device(xhci_device_t *device) {
  xhci_controller_t *host = device->host;
  xhci_port_t *port = device->port;

  if (xhci_enable_interrupter(host, device->interrupter) < 0) {
    EPRINTF("failed to enable interrupter for device on port %d\n", port->number);
    return -1;
  }

  // setup control endpoint
  xhci_endpoint_t *ep0 = device->endpoints[0];
  ep0->ctx->max_packt_sz = get_default_ep0_packet_size(device->port);
  device->ictx->slot->intrptr_target = device->interrupter->index;
  host->dcbaap[device->slot_id] = virt_to_phys(device->dctx->buffer);

  // address device
  if (xhci_run_address_device_cmd(host, device) < 0) {
    EPRINTF("failed to address device\n");
    return -1;
  }

  // DPRINTF("device initialized on port %d\n", device->port->number);
  return 0;
}

int xhci_add_device_endpoint(xhci_endpoint_t *ep) {
  xhci_controller_t *host = ep->host;
  xhci_device_t *device = ep->device;
  device->ictx->ctrl->add_flags = 1 | (1 << (ep->index + 1));
  device->ictx->ctrl->drop_flags = 0;

  if (xhci_run_evaluate_ctx_cmd(host, device) < 0) {
    EPRINTF("failed to evaluate context\n");
    return -1;
  }
  return 0;
}

xhci_endpoint_t *xhci_get_device_endpoint(xhci_device_t *device, usb_dir_t direction) {
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

int xhci_run_command_trb(xhci_controller_t *host, xhci_trb_t trb, xhci_trb_t *result) {
  // DPRINTF("running command [type = %d]\n", trb.trb_type);
  xhci_ring_enqueue_trb(host->cmd_ring, trb);

  // ring the host doorbell
  write32(host->db_base, XHCI_DB(0), DB_TARGET(0));
  xhci_cmd_compl_evt_trb_t res_trb;
  if (chan_recv(host->cmd_compl_ch, &res_trb) < 0) {
    EPRINTF("failed to await command trb on channel\n");
    return -1;
  }

  // DPRINTF("event received\n");
  if (result != NULL) {
    memcpy(result, &res_trb, sizeof(xhci_trb_t));
  }
  return res_trb.compl_code == CC_SUCCESS;
}

int xhci_run_noop_cmd(xhci_controller_t *host) {
  xhci_noop_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_NOOP_CMD;

  xhci_cmd_compl_evt_trb_t result;
  if (xhci_run_command_trb(host, cast_trb(&cmd), (void *) &result) < 0) {
    return -1;
  }
  return 0;
}

int xhci_run_enable_slot_cmd(xhci_controller_t *host, xhci_port_t *port) {
  xhci_enabl_slot_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_ENABL_SLOT_CMD;
  cmd.slot_type = port->protocol->slot_type;

  xhci_cmd_compl_evt_trb_t result;
  if (xhci_run_command_trb(host, cast_trb(&cmd), upcast_trb_ptr(&result)) < 0) {
    return -1;
  }
  return result.slot_id;
}

int xhci_run_address_device_cmd(xhci_controller_t *host, xhci_device_t *device) {
  // DPRINTF("addressing device\n");
  xhci_addr_dev_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_ADDR_DEV_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return xhci_run_command_trb(host, cast_trb(&cmd), NULL);
}

int xhci_run_configure_ep_cmd(xhci_controller_t *host, xhci_device_t *device) {
  // DPRINTF("configuring endpoint\n");
  xhci_config_ep_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_CONFIG_EP_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return xhci_run_command_trb(host, cast_trb(&cmd), NULL);
}

int xhci_run_evaluate_ctx_cmd(xhci_controller_t *host, xhci_device_t *device) {
  DPRINTF("evaluating context\n");
  xhci_eval_ctx_cmd_trb_t cmd;
  cmd.trb_type = TRB_EVAL_CTX_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = virt_to_phys(device->ictx->buffer);
  return xhci_run_command_trb(host, cast_trb(&cmd), NULL);
}

//
// MARK: Transfers
//

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t setup, uint8_t type) {
  if (type != SETUP_DATA_NONE && type != SETUP_DATA_OUT && type != SETUP_DATA_IN) {
    EPRINTF("xhci: invalid setup data type\n");
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

  xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int xhci_queue_data(xhci_device_t *device, uintptr_t buffer, uint16_t length, usb_dir_t direction) {
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

  xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int xhci_queue_status(xhci_device_t *device, usb_dir_t direction, bool ioc) {
  xhci_endpoint_t *ep = device->endpoints[0];

  xhci_status_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_STATUS_STAGE;
  trb.intr_trgt = device->interrupter->index;
  trb.dir = direction;
  trb.ioc = ioc;

  xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int xhci_queue_transfer(xhci_device_t *device, xhci_endpoint_t *ep, uintptr_t buffer, uint16_t length, bool ioc) {
  xhci_normal_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_NORMAL;
  trb.buf_ptr = buffer;
  trb.trs_length = length;
  trb.intr_trgt = device->interrupter->index;
  trb.isp = 0;
  trb.ioc = ioc;

  xhci_ring_enqueue_trb(ep->xfer_ring, cast_trb(&trb));
  return 0;
}

int xhci_ring_start_transfer(xhci_device_t *device, xhci_endpoint_t *ep) {
  xhci_controller_t *hc = device->host;

  // ring the slot doorbell
  uint8_t target = ep->index + 1;
  write32(hc->db_base, XHCI_DB(device->slot_id), DB_TARGET(target));
  return 0;
}

int xhci_await_transfer(xhci_device_t *device, xhci_endpoint_t *ep, xhci_trb_t *result) {
  int res;
  xhci_transfer_evt_trb_t evt_trb;
  if ((res = chan_recv(ep->xfer_ch, &evt_trb)) < 0) {
    EPRINTF("failed to await transfer on channel {:err}\n", res);
    return -1;
  }

  ASSERT(evt_trb.trb_type == TRB_TRANSFER_EVT);
  if (result != NULL) {
    *result = cast_trb(&evt_trb);
  }
  return evt_trb.compl_code == CC_SUCCESS;
}

//
// MARK: Structures
//

xhci_controller_t *xhci_alloc_controller(pci_device_t *pci_dev, pci_bar_t *bar) {
  ASSERT(bar->kind == 0);
  ASSERT(bar->phys_addr != 0);
  ASSERT(bar->virt_addr != 0);

  xhci_controller_t *host = kmallocz(sizeof(xhci_controller_t));
  host->pci_dev = pci_dev;
  host->pid = -1; // set once the process is created
  host->address = bar->virt_addr;
  host->phys_addr = bar->phys_addr;

  host->cap_base = host->address;
  host->op_base = host->address + CAP_LENGTH(read32(host->cap_base, XHCI_CAP_LENGTH));
  host->db_base = host->address + DBOFF_OFFSET(read32(host->cap_base, XHCI_CAP_DBOFF));
  host->rt_base = host->address + RTSOFF_OFFSET(read32(host->cap_base, XHCI_CAP_RTSOFF));
  host->xcap_base = host->address + HCCPARAMS1_XECP(read32(host->cap_base, XHCI_CAP_HCCPARAMS1));

  host->dcbaap = NULL;
  host->intr_numbers = create_bitmap(CAP_MAX_INTRS(read32(host->cap_base, XHCI_CAP_HCSPARAMS1)));
  host->interrupter = xhci_alloc_interrupter(host, xhci_host_irq_handler, host);
  host->protocols = xhci_alloc_protocols(host);
  host->ports = xhci_alloc_ports(host);
  host->devices = NULL;

  host->cmd_ring = xhci_alloc_ring(CMD_RING_SIZE);
  host->evt_ring = host->interrupter->ring;

  host->cmd_compl_ch = chan_alloc(EVT_RING_SIZE, sizeof(xhci_trb_t), CHAN_NOBLOCK, "xhci_cmd_compl_ch");
  host->xfer_evt_ch = chan_alloc(EVT_RING_SIZE, sizeof(xhci_trb_t), CHAN_NOBLOCK, "xhci_xfer_evt_ch");
  host->port_sts_ch = chan_alloc(EVT_RING_SIZE, sizeof(xhci_trb_t), CHAN_NOBLOCK, "xhci_port_sts_ch");

  // allocate device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * CAP_MAX_SLOTS(read32(host->cap_base, XHCI_CAP_HCSPARAMS1));
  void *dcbaap = kmalloca(dcbaap_size, 64);
  memset(dcbaap, 0, dcbaap_size);
  host->dcbaap = dcbaap;

  mtx_init(&host->lock, 0, "xhci_controller_lock");
  return host;
}

xhci_protocol_t *xhci_alloc_protocols(xhci_controller_t *host) {
  LIST_HEAD(xhci_protocol_t) protocols = LIST_HEAD_INITR;

  uint32_t *cap = NULL;
  while (true) {
    cap = get_capability_pointer(host, XHCI_CAP_PROTOCOL, cap);
    if (cap == NULL) {
      break;
    }

    uint8_t rev_minor = (cap[0] >> 16) & 0xFF;
    uint8_t rev_major = (cap[0] >> 24) & 0xFF;
    uint8_t port_offset = cap[2] & 0xFF;
    uint8_t port_count = (cap[2] >> 8) & 0xFF;
    uint8_t slot_type = cap[3] & 0x1F;

    if (rev_major == XHCI_REV_MAJOR_2) {
      ASSERT(rev_minor == XHCI_REV_MINOR_0);
    }

    DPRINTF("supported protocol 'USB %x.%x' (%d ports)\n", rev_major, rev_minor / 0x10, port_count);

    xhci_protocol_t *protocol = kmalloc(sizeof(xhci_protocol_t));
    protocol->rev_major = rev_major;
    protocol->rev_minor = rev_minor;
    protocol->port_offset = port_offset;
    protocol->port_count = port_count;
    protocol->slot_type = slot_type;
    LIST_ADD(&protocols, protocol, list);
  }

  return LIST_FIRST(&protocols);
}

xhci_port_t *xhci_alloc_ports(xhci_controller_t *host) {
  LIST_HEAD(xhci_port_t) ports = LIST_HEAD_INITR;

  xhci_protocol_t *protocol = NULL;
  RLIST_FOREACH(protocol, host->protocols, list) {
    uint8_t offset = protocol->port_offset;
    uint8_t count = protocol->port_count;
    for (int i = offset; i < offset + count; i++) {
      xhci_port_t *port = kmalloc(sizeof(xhci_port_t));
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

xhci_interrupter_t *xhci_alloc_interrupter(xhci_controller_t *host, irq_handler_t fn, void *data) {
  index_t n = bitmap_get_set_free(host->intr_numbers);
  ASSERT(n >= 0);

  int irq = irq_alloc_software_irqnum();
  ASSERT(irq >= 0);
  irq_register_handler(irq, fn, data);
  irq_enable_msi_interrupt(irq, n, host->pci_dev);

  size_t erst_size = sizeof(xhci_erst_entry_t) * ERST_SIZE;
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  xhci_ring_t *ring = xhci_alloc_ring(EVT_RING_SIZE);
  erst[0].rs_addr = xhci_ring_device_ptr(ring);
  erst[0].rs_size = xhci_ring_size(ring);

  xhci_interrupter_t *intr = kmallocz(sizeof(xhci_interrupter_t));
  intr->index = n;
  intr->vector = irq;
  intr->ring = ring;
  intr->erst = (uintptr_t) erst;
  return intr;
}

int xhci_free_interrupter(xhci_interrupter_t *intr) {
  xhci_free_ring(intr->ring);
  kfree((void *) intr->erst);
  // TODO: irq free vector
  kfree(intr);
  return 0;
}

xhci_device_t *xhci_alloc_device(xhci_controller_t *host, xhci_port_t *port, uint8_t slot_id) {
  xhci_device_t *device = kmalloc(sizeof(xhci_device_t));
  memset(device, 0, sizeof(xhci_device_t));

  device->host = host;
  device->port = port;

  device->slot_id = slot_id;
  device->ictx = xhci_alloc_input_ctx(device);
  device->dctx = xhci_alloc_device_ctx(device);

  device->interrupter = xhci_alloc_interrupter(host, xhci_device_irq_handler, device);
  device->evt_ring = device->interrupter->ring;
  LIST_ENTRY_INIT(&device->list);

  mtx_init(&device->lock, 0, "xhci_device_lock");
  cond_init(&device->event, 0);
  return device;
}

int xhci_free_device(xhci_device_t *device) {
  xhci_free_input_ctx(device->ictx);
  xhci_free_device_ctx(device->dctx);
  xhci_free_interrupter(device->interrupter);

  for (int i = 0; i < MAX_ENDPOINTS; i++) {
    xhci_endpoint_t *ep = device->endpoints[i];
    if (ep != NULL) {
      xhci_free_endpoint(ep);
    }
  }

  // TODO: delete thread
  return 0;
}

xhci_endpoint_t *xhci_alloc_endpoint(xhci_device_t *device, uint8_t number, uint8_t type) {
  xhci_controller_t *host = device->host;
  xhci_endpoint_t *ep = kmalloc(sizeof(xhci_endpoint_t));
  memset(ep, 0, sizeof(xhci_endpoint_t));
  ep->host = host;
  ep->device = device;
  ep->number = number;
  ep->index = get_ep_ctx_index(number, type);
  ep->type = type;
  ep->ctx = device->ictx->endpoint[ep->index];
  ep->xfer_ring = xhci_alloc_ring(XFER_RING_SIZE);
  ep->xfer_ch = chan_alloc(EVT_RING_SIZE, sizeof(xhci_trb_t), CHAN_NOBLOCK, "xhci_endpoint_xfer_ch");

  ep->ctx->tr_dequeue_ptr = xhci_ring_device_ptr(ep->xfer_ring) | ep->xfer_ring->cycle;
  return ep;
}

int xhci_free_endpoint(xhci_endpoint_t *ep) {
  xhci_free_ring(ep->xfer_ring);
  chan_free(ep->xfer_ch);
  kfree(ep);
  return 0;
}


xhci_ictx_t *xhci_alloc_input_ctx(xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  void *ptr = vmalloc(PAGE_SIZE, VM_RDWR|VM_NOCACHE);

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

int xhci_free_input_ctx(xhci_ictx_t *ictx) {
  vfree(ictx->buffer);
  kfree(ictx);
  return 0;
}

xhci_dctx_t *xhci_alloc_device_ctx(xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  xhci_dctx_t *dctx = kmallocz(sizeof(xhci_dctx_t));
  dctx->buffer = vmalloc(PAGE_SIZE, VM_RDWR|VM_NOCACHE);
  dctx->slot = dctx->buffer;
  for (int i = 0; i < 31; i++) {
    dctx->endpoint[i] = offset_ptr(dctx->buffer, ctxsz * (i + 2));
  }

  return dctx;
}

int xhci_free_device_ctx(xhci_dctx_t *dctx) {
  vfree(dctx->buffer);
  kfree(dctx);
  return 0;
}

//
// MARK: TRB Rings
//

xhci_ring_t *xhci_alloc_ring(size_t capacity) {
  xhci_ring_t *ring = kmallocz(sizeof(xhci_ring_t));
  ring->base = vmalloc(capacity * sizeof(xhci_trb_t), VM_RDWR|VM_ZERO);
  ring->index = 0;
  ring->max_index = capacity;
  ring->cycle = 1;
  sem_init(&ring->events, 0, SEM_SPIN, "xhci_ring_events");
  return ring;
}

void xhci_free_ring(xhci_ring_t *ring) {
  vfree(ring->base);
  kfree(ring);
}

int xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t trb) {
  ASSERT(trb.trb_type != 0);
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

bool xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t *out) {
  ASSERT(out != NULL);
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

uint64_t xhci_ring_device_ptr(xhci_ring_t *ring) {
  return virt_to_phys(ring->base) + (ring->index * sizeof(xhci_trb_t));
}

size_t xhci_ring_size(xhci_ring_t *ring) {
  return ring->max_index * sizeof(xhci_trb_t);
}

//
// MARK: USB Host Interface
//

int xhci_usb_host_init(usb_host_t *usb_host) {
  xhci_controller_t *host = usb_host_to_host(usb_host);

  // reset controller to starting state
  if (xhci_reset_controller(host) < 0) {
    EPRINTF("failed to reset controller\n");
    xhci_halt_controller(host);
    return -1;
  }

  // then setup the controller
  if (xhci_setup_controller(host) < 0) {
    EPRINTF("failed to setup controller\n");
    return -1;
  }
  return 0;
}

int xhci_usb_host_start(usb_host_t *usb_host) {
  xhci_controller_t *host = usb_host_to_host(usb_host);

  // run controller
  if (xhci_run_controller(host) < 0) {
    EPRINTF("failed to start controller\n");
    return -1;
  }
  return 0;
}

int xhci_usb_host_stop(usb_host_t *usb_host) {
  xhci_controller_t *host = usb_host_to_host(usb_host);

  // halt controller
  if (xhci_halt_controller(host) < 0) {
    EPRINTF("failed to stop controller\n");
    return -1;
  }
  return 0;
}

int xhci_usb_host_discover(usb_host_t *usb_host) {
  xhci_controller_t *host = usb_host_to_host(usb_host);

  xhci_port_t *port;
  RLIST_FOREACH(port, host->ports, list) {
    uint8_t n = port->number - 1;
    uint32_t portsc = read32(host->op_base, XHCI_PORT_SC(n));
    if (portsc & PORTSC_CCS) {
      DPRINTF("device connected to port %d\n", port->number);
      if (usb_handle_device_connect(usb_host, port) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

usb_host_impl_t xhci_usb_host_impl = {
  .init = xhci_usb_host_init,
  .start = xhci_usb_host_start,
  .stop = xhci_usb_host_stop,
  .discover = xhci_usb_host_discover,
};

//
// MARK: USB Device Interface
//

int xhci_usb_device_init(usb_device_t *usb_dev) {
  xhci_controller_t *host = usb_dev_to_host(usb_dev);
  xhci_port_t *port = usb_dev->host_data;

  // enable port
  DPRINTF("enabling port %d\n", port->number);
  if (xhci_enable_port(host, port) < 0) {
    EPRINTF("failed to enable port %d\n", port->number);
    return -1;
  }

  // enable slot to use with the device
  DPRINTF("enabling slot for port %d\n", port->number);
  int slot_id = xhci_run_enable_slot_cmd(host, port);
  if (slot_id < 0) {
    EPRINTF("failed to enable slot for port %d\n", port->number);
    return -1;
  }

  xhci_device_t *dev = xhci_alloc_device(host, port, slot_id);
  ASSERT(dev != NULL);
  dev->usb_device = usb_dev;

  DPRINTF("creating thread for device on port %d\n", port->number);

  {
    // create a thread to handle events from the device
    thread_t *td = thread_alloc(TDF_KTHREAD, SIZE_16KB);
    thread_setup_entry(td, (uintptr_t) xhci_device_event_loop, 1, dev);
    thread_setup_name(td, cstr_make("xhci_device_event_loop"));

    // add the thread to the host controller process
    __ref proc_t *host_proc = proc_lookup(host->pid);
    ASSERT(host_proc != NULL);
    proc_add_thread(host_proc, td);
    pr_putref(&host_proc);
  }

  dev->endpoints[0] = xhci_alloc_endpoint(dev, 0, XHCI_CTRL_BI_EP);
  dev->endpoints[0]->ctx->max_packt_sz = get_default_ep0_packet_size(dev->port);
  dev->ictx->slot->intrptr_target = dev->interrupter->index;

  // setup the device
  if (xhci_setup_device(dev) < 0) {
    EPRINTF("failed to setup device on port %d\n", port->number);
    xhci_free_device(dev);
    return -1;
  }

  port->device = dev;
  usb_dev->host_data = dev;
  return 0;
}

int xhci_usb_device_deinit(usb_device_t *usb_dev) {
  // TODO: tear down things properly
  xhci_controller_t *host = usb_dev_to_host(usb_dev);
  xhci_device_t *dev = usb_dev_to_device(usb_dev);
  xhci_free_device(dev);
  kfree(dev);
  return 0;
}

int xhci_usb_device_add_transfer(usb_device_t *usb_dev, usb_endpoint_t *endpoint, usb_transfer_t *transfer) {
  xhci_device_t *dev = usb_dev_to_device(usb_dev);
  xhci_endpoint_t *ep = endpoint->host_data;
  ASSERT(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  if (transfer->type == USB_SETUP_XFER) {
    usb_setup_packet_t packet = transfer->setup;

    // setup control transfer
    if (transfer->buffer == 0) {
      // no data stage
      xhci_queue_setup(dev, packet, SETUP_DATA_NONE);
      xhci_queue_status(dev, USB_OUT, true);
    } else {
      // has data stage
      bool is_pkt_in = packet.request_type.direction == USB_SETUP_DEV_TO_HOST;
      usb_dir_t dir = is_pkt_in ? USB_IN : USB_OUT;
      uint8_t type = is_pkt_in ?  SETUP_DATA_IN : SETUP_DATA_OUT;

      xhci_queue_setup(dev, packet, type);
      xhci_queue_data(dev, transfer->buffer, transfer->length, dir);
      xhci_queue_status(dev, USB_OUT, false);
    }
  } else {
    // data transfer

    // a usb transfer with the USB_XFER_PART flag set are intended to
    // be followed by more transfers so only interrupt on the last one.
    bool ioc = (transfer->flags & USB_XFER_PART) == 0;

    // TODO: length usize -> u16 overflow check/auto split into multiple xfers
    ASSERT(transfer->length <= UINT16_MAX);
    xhci_queue_transfer(dev, ep, transfer->buffer, transfer->length, ioc);
  }

  return 0;
}

int xhci_usb_device_start_transfer(usb_device_t *usb_dev, usb_endpoint_t *endpoint) {
  xhci_device_t *dev = usb_dev_to_device(usb_dev);
  xhci_endpoint_t *ep = endpoint->host_data;
  kassert(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  if (xhci_ring_start_transfer(dev, ep) < 0) {
    EPRINTF("failed to start transfer\n");
    return -1;
  }

  return 0;
}

int xhci_usb_device_await_event(usb_device_t *usb_dev, usb_endpoint_t *endpoint, usb_event_t *event) {
  xhci_device_t *dev = usb_dev_to_device(usb_dev);
  xhci_endpoint_t *ep = endpoint->host_data;
  ASSERT(ep->type == get_xhci_ep_type(endpoint->type, endpoint->dir));

  xhci_transfer_evt_trb_t result;
  if (xhci_await_transfer(dev, ep, cast_trb_ptr(&result)) < 0) {
    EPRINTF("failed to wait for transfer\n");
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
    DPRINTF("xhci_usb_device_await_event() | USB ERROR %d\n", result.compl_code);
  }
  return 0;
}

int xhci_usb_device_read_descriptor(usb_device_t *usb_dev, usb_device_descriptor_t **out) {
  xhci_controller_t *host = usb_dev_to_host(usb_dev);
  xhci_device_t *dev = usb_dev_to_device(usb_dev);
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

    xhci_queue_setup(dev, get_desc0, SETUP_DATA_IN);
    xhci_queue_data(dev, kheap_ptr_to_phys(temp), 8, DATA_IN);
    xhci_queue_status(dev, DATA_OUT, false);
    if (xhci_ring_start_transfer(dev, dev->endpoints[0]) < 0) {
      EPRINTF("failed to initiate transfer for device descriptor\n");
      return -1;
    }

    xhci_transfer_evt_trb_t result;
    if (xhci_await_transfer(dev, dev->endpoints[0], cast_trb_ptr(&result)) < 0) {
      EPRINTF("failed to get device descriptor\n");
      kfree(temp);
      return -1;
    }

    // DPRINTF("  device descriptor (0)\n");
    // DPRINTF("    length = %d | usb version = %x\n", temp->length, temp->usb_ver);
    // DPRINTF("    max packet sz (ep0) = %d\n", temp->max_packt_sz0);

    // update default control ep max packet size
    dev->ictx->ctrl->add_flags |= 1; // set A1 bit to 1
    dev->ictx->endpoint[0]->max_packt_sz = temp->max_packt_sz0;

    // evaluate context
    if (xhci_run_evaluate_ctx_cmd(host, dev) < 0) {
      EPRINTF("failed to evaluate context\n");
      kfree(temp);
      return -1;
    }
    // DPRINTF("context evaluated\n");
    kfree(temp);
  }

  // read full descriptor
  size_t size = sizeof(usb_device_descriptor_t);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(DEVICE_DESCRIPTOR, 0, size);
  usb_device_descriptor_t *desc = kmallocz(size);

  xhci_queue_setup(dev, get_desc, SETUP_DATA_IN);
  xhci_queue_data(dev, kheap_ptr_to_phys(desc), size, DATA_IN);
  xhci_queue_status(dev, DATA_OUT, false);

  if (xhci_ring_start_transfer(dev, dev->endpoints[0]) < 0) {
    EPRINTF("failed to initiate transfer for device descriptor\n");
    return -1;
  }

  xhci_transfer_evt_trb_t result;
  if (xhci_await_transfer(dev, dev->endpoints[0], cast_trb_ptr(&result)) < 0) {
    EPRINTF("failed to get device descriptor\n");
    kfree(desc);
    return -1;
  }

  ASSERT(out != NULL);
  *out = desc;
  return 0;
}

//

int xhci_usb_init_endpoint(usb_endpoint_t *usb_ep) {
  xhci_controller_t *host = usb_dev_to_host(usb_ep->device);
  xhci_device_t *dev = usb_dev_to_device(usb_ep->device);
  xhci_ictx_t *ictx = dev->ictx;
  if (usb_ep->number == 0) {
    // special default control endpoint
    usb_ep->host_data = dev->endpoints[0];
    dev->endpoints[0]->usb_endpoint = usb_ep;
    return 0;
  }

  uint8_t ep_num = usb_ep->number;
  uint8_t ep_type = get_xhci_ep_type(usb_ep->type, usb_ep->dir);
  // DPRINTF("initializing endpoint | index = %d, number = %d, type = %d (%s)\n",
  //         get_ep_ctx_index(ep_num, ep_type), ep_num, ep_type, xhci_ep_type_names[ep_type]);

  xhci_endpoint_t *ep = xhci_alloc_endpoint(dev, ep_num, ep_type);
  xhci_endpoint_ctx_t *ctx = ep->ctx;
  ctx->ep_type = ep_type;
  ctx->max_packt_sz = usb_ep->max_pckt_sz;
  ctx->interval = usb_ep->interval;
  ctx->max_burst_sz = 1;
  ctx->avg_trb_length = 8;
  ctx->max_streams = 0;
  ctx->mult = 0;
  ctx->cerr = 0;

  ictx->slot->ctx_entries++;
  ictx->ctrl->drop_flags = 0;
  ictx->ctrl->add_flags = 1 | (1 << (ep->index + 1));
  if (xhci_run_configure_ep_cmd(host, dev) < 0) {
    EPRINTF("failed to add endpoint\n");
    xhci_free_endpoint(ep);
    return -1;
  }

  dev->endpoints[ep->index] = ep;
  usb_ep->host_data = ep;
  ep->usb_endpoint = usb_ep;
  return 0;
}

int xhci_usb_deinit_endpoint(usb_endpoint_t *usb_ep) {
  xhci_controller_t *host = usb_dev_to_host(usb_ep->device);
  xhci_device_t *dev = usb_dev_to_device(usb_ep->device);
  xhci_endpoint_t *ep = usb_ep->host_data;
  xhci_ictx_t *ictx = dev->ictx;

  ictx->slot->ctx_entries--;
  ictx->ctrl->drop_flags = (1 << (ep->index + 1));
  ictx->ctrl->add_flags = 1;
  if (xhci_run_configure_ep_cmd(host, dev) < 0) {
    EPRINTF("failed to drop endpoint\n");
    // TODO: what to do here?
    ictx->slot->ctx_entries = dev->dctx->slot->ctx_entries;
    return -1;
  }

  xhci_free_endpoint(ep);
  usb_ep->host_data = NULL;
  return 0;
}

usb_device_impl_t xhci_usb_device_impl = {
  .init = xhci_usb_device_init,
  .deinit = xhci_usb_device_deinit,
  .add_transfer = xhci_usb_device_add_transfer,
  .start_transfer = xhci_usb_device_start_transfer,
  .await_event = xhci_usb_device_await_event,
  .read_device_descriptor = xhci_usb_device_read_descriptor,

  .init_endpoint = xhci_usb_init_endpoint,
  .deinit_endpoint = xhci_usb_deinit_endpoint,
};

//
// MARK: Device/Driver Interface
//

bool xhci_driver_check_device(struct device_driver *drv, struct device *dev) {
  pci_device_t *pci_dev = dev->bus_device;
  return pci_dev->class_code == PCI_SERIAL_BUS_CONTROLLER &&
         pci_dev->subclass == PCI_USB_CONTROLLER &&
         pci_dev->prog_if == USB_PROG_IF_XHCI;
}

int xhci_driver_setup_device(struct device *dev) {
  pci_device_t *pci_dev = dev->bus_device;

  pci_bar_t *bar = SLIST_FIND(b, pci_dev->bars, next, b->kind == 0/* memory bar */);
  if (bar == NULL) {
    EPRINTF("failed to register controller: no bars found\n");
    return -1;
  }

  // check for duplicate host
  xhci_controller_t *existing = LIST_FIND(h, &hosts, list, h->phys_addr == bar->phys_addr);
  if (existing != NULL) {
    EPRINTF("failed to register controller: already registered\n");
    goto fail;
  }

  // map the xhci into the virtual memory space
  bar->virt_addr = vmap_phys(bar->phys_addr, 0, align(bar->size, PAGE_SIZE), VM_RDWR|VM_NOCACHE, "xhci");
  if (bar->virt_addr == 0) {
    EPRINTF("failed to map controller into memory\n");
    goto fail;
  }

  if (!HCCPARAMS1_AC64(read32(bar->virt_addr, XHCI_CAP_HCCPARAMS1))) {
    // we dont support 32-bit controllers right now
    EPRINTF("controller not supported (64-bit only)\n");
    goto fail;
  }

  uint16_t version = CAP_VERSION(read32(bar->virt_addr, XHCI_CAP_LENGTH));
  uint8_t version_maj = (version >> 8) & 0xFF;
  uint8_t version_min = version & 0xFF;

  // allocate the xhci controller struct
  xhci_controller_t *host = xhci_alloc_controller(pci_dev, bar);
  if (host == NULL) {
    EPRINTF("failed to allocate xhci controller\n");
    goto fail;
  }
  dev->data = host;

  DPRINTF("registering controller %d\n", num_hosts);
  LIST_ADD(&hosts, host, list);
  num_hosts++;

  {
    // create a new process for the host controller
    __ref proc_t *driver_proc = proc_alloc_new(getref(curproc->creds));
    host->pid = driver_proc->pid;

    // and setup the main thread to handle controller events
    proc_setup_add_thread(driver_proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
    proc_setup_entry(driver_proc, (uintptr_t) xhci_controller_event_loop, 1, host);
    proc_setup_name(driver_proc, cstr_make("xhci_driver"));
    proc_finish_setup_and_submit_all(moveref(driver_proc));
  }
  sched_again(SCHED_YIELDED);

  // register the usb host
  usb_host_t *usb_host = kmallocz(sizeof(usb_host_t));
  usb_host->name = kasprintf("xHCI Controller v%x.%x", version_maj, version_min);
  usb_host->pci_device = pci_dev;
  usb_host->host_impl = &xhci_usb_host_impl;
  usb_host->device_impl = &xhci_usb_device_impl;
  usb_host->data = host;
  usb_host->root = NULL;
  if (usb_register_host(usb_host) < 0) {
    EPRINTF("failed to register usb host '%s'\n", usb_host->name);
    kfreep(&usb_host->name);
    kfreep(&usb_host);
  }
  return 0;

LABEL(fail);
  if (bar->virt_addr != 0)
    vmap_free(bar->virt_addr, bar->size);
  bar->virt_addr = 0;
  return -1;
}

int xhci_driver_remove_device(struct device *dev) {
  todo();
  return 0;
}

static struct device_ops xhci_device_ops = {
  .d_open = NULL,
  .d_close = NULL,
  .d_read = NULL,
  .d_write = NULL,
  .d_getpage = NULL,
  .d_putpage = NULL,
};

static struct device_driver xhci_device_driver = {
  .name = "xhci",
  .data = NULL,
  .ops = &xhci_device_ops,
  .check_device = xhci_driver_check_device,
  .setup_device = xhci_driver_setup_device,
  .remove_device = xhci_driver_remove_device,
};

static void xhci_module_init() {
  if (register_driver("pci", &xhci_device_driver) < 0) {
    panic("xhci: failed to register driver");
  }
}
MODULE_INIT(xhci_module_init);

//
// MARK: Debugging
//

void xhci_debug_host_registers(xhci_controller_t *hc) {
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

void xhci_debug_port_registers(xhci_controller_t *hc, xhci_port_t *port) {
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
