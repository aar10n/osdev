//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#include <usb/xhci.h>
#include <usb/xhci_hw.h>

#include <usb/usb.h>
#include <bus/pcie.h>
#include <cpu/io.h>
#include <acpi/pm_timer.h>

#include <mm.h>
#include <irq.h>
#include <sched.h>
#include <mutex.h>
#include <clock.h>
#include <printf.h>
#include <panic.h>
#include <string.h>

#include <atomic.h>

#define CMD_RING_SIZE  256
#define EVT_RING_SIZE  256
#define XFER_RING_SIZE 256

#define QDEBUG(v) outdw(0x888, v)

#define xhci_log(str, args...) kprintf("[xhci] " str "\n", ##args)

#define XHCI_DEBUG
#ifdef XHCI_DEBUG
#define xhci_trace_debug(str, args...) kprintf("[xhci] " str "\n", ##args)
#else
#define xhci_trace_debug(str, args...)
#endif

#define WAIT_TIMEOUT(cond, fail) \
{                                \
  int attempts = 0;              \
  while (cond) {                 \
    if (attempts >= 5) {         \
      fail;                      \
    }                            \
    thread_sleep(1000);          \
    attempts++;                  \
  }                              \
}

// #define xhci_cond_timeout(cond_expr, timeout_ns) \
//   ({                                \
//     bool cond = false;              \
//     clock_t future = clock_future_time(timeout_ns); \
//     while (!is_future_time(future)) { \
//       if (cond_expr) {              \
//         cond = true;                \
//         break;                      \
//       }                             \
//     }                               \
//     cond;                           \
//   })

#define WAIT_READY(fail)                         \
{                                                \
  int attempts = 0;                              \
  while (xhci_op->usbsts_r & USBSTS_NOT_READY) { \
    if (attempts >= 5) {                         \
      fail;                                      \
    }                                            \
    thread_sleep(1000);                          \
    attempts++;                                  \
  }                                              \
}

void _xhci_debug_host_registers(xhci_controller_t *hc);
void _xhci_debug_device_context(_xhci_device_t *device);

static xhci_dev_t *xhc = NULL;
static uint8_t intr_vector = 0x32;
static uint8_t intr_number = 0;
static int num_controllers = 0;
static LIST_HEAD(xhci_controller_t) controllers;

static inline int get_ep_type(uint8_t ep_type, uint8_t ep_dir) {
  switch (ep_type) {
    case 0:
      return XHCI_CTRL_BI_EP;
    case 1:
      return ep_dir == 0 ? XHCI_ISOCH_OUT_EP : XHCI_ISOCH_IN_EP;
    case 2:
      return ep_dir == 0 ? XHCI_BULK_OUT_EP : XHCI_BULK_IN_EP;
    case 3:
      return ep_dir == 0 ? XHCI_INTR_OUT_EP : XHCI_INTR_IN_EP;
    default:
      return 0;
  }
}

static inline const char *get_speed_str(uint8_t speed) {
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

static inline bool is_64_byte_context(xhci_controller_t *hc) {
  return HCCPARAMS1_CSZ(read32(hc->cap_base, XHCI_CAP_HCCPARAMS1));
}

//
// MARK:
//

static void irq_callback(uint8_t vector, void *data) {
  xhci_dev_t *xhci = data;
  if (xhci_op->usbsts.evt_int) {
    cond_signal(&xhci->event);
  }
}

static void device_irq_callback(uint8_t vector, void *data) {
  xhci_device_t *device = data;
  xhci_dev_t *xhci = device->xhci;
  if (xhci_op->usbsts.evt_int) {
    cond_signal(&device->event);
  }
}

//

void xhci_host_irq_handler(uint8_t vector, void *data) {
  xhci_controller_t *hc = data;
  kprintf(">>>>> xhci: controller interrupt! <<<<<\n");
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
  } else if (!(usbsts & USBSTS_EVT_INT)) {
    return;
  }

  cond_signal(&hc->evt_ring->cond);
}

void xhci_device_irq_handler(uint8_t vector, void *data) {
  _xhci_device_t *device = data;
  xhci_controller_t *hc = device->host;
  uint8_t n = device->interrupters->index;
  kprintf(">>>>> xhci: device interrupt! <<<<<\n");

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

int _xhci_handle_event(xhci_controller_t *hc, xhci_trb_t trb) {
  kprintf("xhci: handling event [type = %d]\n", trb.trb_type);
  if (trb.trb_type == TRB_TRANSFER_EVT) {
    kprintf("xhci: transfer complete\n");
    cond_clear_signal(&hc->xfer_cond);
    hc->xfer_trb = trb;
    cond_signal(&hc->xfer_cond);
  } else if (trb.trb_type == TRB_CMD_CMPL_EVT) {
    kprintf("xhci: command completed\n");
    cond_clear_signal(&hc->cmd_compl_cond);
    hc->cmd_compl_trb = trb;
    cond_signal(&hc->cmd_compl_cond);
  } else if (trb.trb_type == TRB_PORT_STS_EVT) {
    xhci_port_status_evt_trb_t *port_trb = (void *) &trb;
    kprintf("     port %d changed\n", port_trb->port_id);

    uint32_t id = port_trb->port_id;
    uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(id));
    portsc |= PORTSC_CSC;
    write32(hc->op_base, XHCI_PORT_SC(id), portsc);
  }
  return 0;
}

noreturn void *_xhci_controller_event_loop(void *arg) {
  xhci_controller_t *hc = arg;
  kprintf("xhci: starting controller event loop\n");

  while (true) {
    cond_wait(&hc->evt_ring->cond);

    uint64_t old_erdp = _xhci_ring_device_ptr(hc->evt_ring);
    xhci_trb_t trb;
    while (_xhci_ring_dequeue_trb(hc->evt_ring, &trb)) {
      if (_xhci_handle_event(hc, trb) < 0) {
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
  uint8_t n = device->interrupters->index;
  kprintf("xhci: starting device event loop\n");

  while (true) {
    cond_wait(&device->evt_ring->cond);

    uint64_t old_erdp = _xhci_ring_device_ptr(device->evt_ring);
    xhci_trb_t trb;
    while (_xhci_ring_dequeue_trb(device->evt_ring, &trb)) {
      kprintf("  -> trb %d\n", trb.trb_type);
      kassert(trb.trb_type == TRB_TRANSFER_EVT);

      device->xfer_evt_trb = trb;
      cond_signal(&device->xfer_evt_cond);
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

noreturn void *xhci_event_loop(void *arg) {
  xhci_dev_t *xhci = arg;
  xhci_trace_debug("starting event loop");

  xhci_op->usbsts.evt_int = 1;
  while (true) {
    if (!xhci_op->usbsts.evt_int) {
      // xhci_trace_debug("waiting for event");
      cond_wait(&xhci->event);
    }
    // xhci_trace_debug("an event occurred");

    if (xhci_op->usbsts.hc_error) {
      panic(">>>>>>> HOST CONTROLLER ERROR <<<<<<<");
    } else if (xhci_op->usbsts.hs_err) {
      panic(">>>>>>> HOST SYSTEM ERROR <<<<<<<\n");
    }

    xhci_ring_t *ring = xhci->intr->ring;
    while (xhci_has_dequeue_event(xhci->intr)) {
      xhci_trb_t *trb;
      xhci_ring_dequeue_trb(ring, &trb);
      thread_send(trb);
    }

    uintptr_t addr = PAGE_PHYS_ADDR(ring->page) + (ring->index * sizeof(xhci_trb_t));
    xhci_op->usbsts.evt_int = 1;
    xhci_intr(0)->iman.ip = 1;
    xhci_intr(0)->erdp_r = addr | ERDP_EH_BUSY;

    cond_signal(&xhci->event_ack);
  }
}

noreturn void *xhci_device_event_loop(void *arg) {
  xhci_device_t *device = arg;
  xhci_dev_t *xhci = device->xhci;
  uint8_t n = device->intr->number;
  xhci_trace_debug("starting device event loop");

  xhci_op->usbsts.evt_int = 1;
  while (true) {
    if (!xhci_op->usbsts.evt_int) {
      cond_wait(&device->event);
      xhci_trace_debug("device event!");
    }

    xhci_ring_t *ring = device->intr->ring;
    while (xhci_has_dequeue_event(device->intr)) {
      xhci_trb_t *trb;
      xhci_ring_dequeue_trb(ring, &trb);
      // thread_send(trb);
      device->thread->data = trb;
    }

    uintptr_t addr = PAGE_PHYS_ADDR(ring->page) + (ring->index * sizeof(xhci_trb_t));
    xhci_op->usbsts.evt_int = 1;
    xhci_intr(n)->iman.ip = 1;
    xhci_intr(n)->erdp_r = addr | ERDP_EH_BUSY;

    xhci_trace_debug("signalling");
    cond_signal(&device->event_ack);
  }
}

//
// MARK: Core
//

void _xhci_init(pcie_device_t *device) {
  kassert(device->class_code == PCI_SERIAL_BUS_CONTROLLER);
  kassert(device->subclass == PCI_USB_CONTROLLER);
  kassert(device->prog_if == USB_PROG_IF_XHCI);

  xhci_controller_t *hc = kmalloc(sizeof(xhci_controller_t));
  memset(hc, 0, sizeof(xhci_controller_t));
  hc->pcie_device = device;

  pcie_bar_t *bar = device->bars;
  pcie_bar_t *xhci_bar = NULL;
  while (bar != NULL) {
    if (bar->kind == 0) {
      xhci_bar = bar;
      break;
    }
    bar = bar->next;
  }
  if (xhci_bar == NULL) {
    kprintf("xhci: no memory space found\n");
    kfree(hc);
    return;
  }

  xhci_controller_t *controller = NULL;
  LIST_FOREACH(controller, &controllers, list) {
    if (controller->phys_addr == xhci_bar->phys_addr) {
      // skip duplicate controllers
      kfree(hc);
      return;
    }
  }

  LIST_ADD(&controllers, hc, list);
  num_controllers++;

  kprintf("xhci: initializing controller %d\n", num_controllers);
  kprintf("xhci: bar = %018p [size = %zu]\n", bar->phys_addr, bar->size);
  bar->virt_addr = (uintptr_t) _vmap_mmio(xhci_bar->phys_addr, align(bar->size, PAGE_SIZE), PG_NOCACHE | PG_WRITE);
  _vmap_get_mapping(bar->virt_addr)->name = "xhci";
  hc->address = bar->virt_addr;
  hc->phys_addr = bar->phys_addr;
  hc->cap_base = hc->address;
  cond_init(&hc->cmd_compl_cond, 0);
  cond_init(&hc->xfer_cond, 0);
  clear_trb(&hc->cmd_compl_trb);
  clear_trb(&hc->xfer_trb);

  if (_xhci_init_controller(hc) < 0) {
    return;
  }

  if (_xhci_reset_controller(hc) < 0) {
    kprintf("xhci: failed to reset controller\n");
    _xhci_halt_controller(hc);
    return;
  }

  if (_xhci_start_controller(hc) < 0) {
    kprintf("xhci: failed to start controller\n");
    _xhci_halt_controller(hc);
    return;
  }

  hc->protocols = _xhci_get_protocols(hc);
  hc->ports = _xhci_discover_ports(hc);
  hc->devices = NULL;
  if (_xhci_setup_devices(hc) < 0) {
    kprintf("xhci: failed to setup devices\n");
    WHILE_TRUE;
    return;
  }

  kprintf("xhci: init done\n");
  // WHILE_TRUE;
}

int _xhci_setup_devices(xhci_controller_t *hc) {
  kassert(hc != NULL);
  kprintf("xhci: testing command ring\n");
  if (_xhci_run_noop_cmd(hc) < 0) {
    kprintf("xhci: no-op command failed\n");
    return -1;
  }

  kprintf("xhci: setting up devices\n");
  _xhci_port_t *port = NULL;
  RLIST_FOREACH(port, hc->ports, list) {
    kprintf("xhci: port %d - %s\n", port->number, get_speed_str(port->speed));
    if (_xhci_enable_port(hc, port) < 0) {
      kprintf("xhci: failed to enable port %d\n", port->number);
      continue;
    }

    kprintf("xhci: enabled port %d\n", port->number);
    int result = _xhci_run_enable_slot_cmd(hc, port);
    if (result < 0) {
      kprintf("xhci: failed to enable slot %d\n", port->number);
      continue;
    }

    kprintf("xhci: enabled slot for port %d [slot_id = %d]\n", port->number, result);

    _xhci_device_t *device = _xhci_setup_device(hc, port, result);
    if (device == NULL) {
      kprintf("xhci: failed to setup device on port %d\n", port->number);
      continue;
    }
    port->device = device;
    RLIST_ADD_FRONT(&hc->devices, device, list);

    if (_xhci_run_address_device_cmd(hc, device) < 0) {
      kprintf("xhci: failed to address device\n");
      continue;
    }
    kprintf("xhci: addressed device\n");

    kprintf("xhci: device context\n");
    _xhci_debug_device_context(device);

    kprintf("xhci: reading device descriptor\n");
    usb_device_descriptor_t *desc = _xhci_get_device_descriptor(device, DEVICE_DESCRIPTOR, 0, NULL);
    if (desc == NULL) {
      kprintf("xhci: failed to get device descriptor\n");
      // TODO: free stuff?
      continue;
    }
    device->desc = desc;
    kprintf("xhci: read device descriptor\n");

    char *device_str = _xhci_get_string_descriptor(device, device->desc->product_idx);
    char *manuf_str = _xhci_get_string_descriptor(device, device->desc->manuf_idx);
    kprintf("xhci: device: %s | manufacturer: %s\n", device_str, manuf_str);
    kfree(device_str);
    kfree(manuf_str);

    device->ictx->ctrl->add_flags |= 1;
    device->ictx->endpoint[0]->max_packt_sz = desc->max_packt_sz0;
    device->ictx->endpoint[0]->max_packt_sz = 8;
    kprintf("xhci: evaluating context\n");
    if (_xhci_run_evaluate_ctx_cmd(hc, device) < 0) {
      kprintf("xhci: failed to evaluate context\n");
      // TODO: free stuff?
      continue;
    }
    kprintf("xhci: context evaluated\n");

    WHILE_TRUE;

    kprintf("xhci: reading device configs\n");
    usb_config_descriptor_t *configs = _xhci_get_device_configs(device);
    if (configs == NULL) {
      kprintf("xhci: failed to get device configs\n");
      continue;
    }

    kprintf("xhci: device configs read\n");
    kprintf("xhci: selecting device config\n");

    if (_xhci_select_device_config(device) < 0) {
      kprintf("xhci: failed to select device config\n");
      continue;
    }

    kprintf("xhci: device config selected\n");
    kprintf("xhci: device enabled\n");
    // usb_register_device(device);

    kprintf("xhci: stopping\n");
    WHILE_TRUE;
  }

  return 0;
}

//
// MARK: Controller
//

int _xhci_init_controller(xhci_controller_t *hc) {
  uint32_t caplength = read32(hc->cap_base, XHCI_CAP_LENGTH);
  kprintf("capability registers:\n");
  kprintf("  length: %u\n", CAP_LENGTH(caplength));
  kprintf("  version: %u\n", CAP_VERSION(caplength));
  hc->op_base = hc->address + CAP_LENGTH(caplength);

  uint32_t hcsparams1 = read32(hc->cap_base, XHCI_CAP_HCSPARAMS1);
  uint32_t max_slots = CAP_MAX_SLOTS(hcsparams1);
  kprintf("  max slots: %d\n", max_slots);
  kprintf("  max interrupters: %d\n", CAP_MAX_INTRS(hcsparams1));
  kprintf("  max ports: %d\n", CAP_MAX_PORTS(hcsparams1));

  uint32_t hccparams1 = read32(hc->cap_base, XHCI_CAP_HCCPARAMS1);
  kprintf("  ac64: %d\n", HCCPARAMS1_AC64(hccparams1));
  kprintf("  csz: %d\n", HCCPARAMS1_CSZ(hccparams1));
  kprintf("  pind: %d\n", HCCPARAMS1_PIND(hccparams1));
  kprintf("  lhrc: %d\n", HCCPARAMS1_LHRC(hccparams1));
  kprintf("  xecp: %d\n", HCCPARAMS1_XECP(hccparams1));
  hc->xcap_base = hc->address + HCCPARAMS1_XECP(hccparams1);
  if (!HCCPARAMS1_AC64(hccparams1)) {
    // we dont support 32-bit controllers right now
    return -ENOTSUP;
  }

  uint32_t dboff = read32(hc->cap_base, XHCI_CAP_DBOFF);
  kprintf("  dboff: %d\n", DBOFF_OFFSET(dboff));
  hc->db_base = hc->address + DBOFF_OFFSET(dboff);

  uint32_t rtsoff = read32(hc->cap_base, XHCI_CAP_RTSOFF);
  kprintf("  rtsoff: %d\n", RTSOFF_OFFSET(rtsoff));
  hc->rt_base = hc->address + RTSOFF_OFFSET(rtsoff);
  return 0;
}

int _xhci_reset_controller(xhci_controller_t *hc) {
  kprintf("xhci: resetting controller\n");
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  usbcmd &= ~USBCMD_RUN;
  usbcmd |= USBCMD_HC_RESET;
  write32(hc->op_base, XHCI_OP_USBCMD, usbcmd);

  while ((read32(hc->op_base, XHCI_OP_USBSTS) & USBSTS_NOT_READY) != 0) {
    cpu_pause();
  }

  kprintf("xhci: controller reset!\n");
  return 0;
}

int _xhci_start_controller(xhci_controller_t *hc) {
  // configure the max slots enabled
  uint32_t max_slots = CAP_MAX_SLOTS(read32(hc->cap_base, XHCI_CAP_HCSPARAMS1));
  write32(hc->op_base, XHCI_OP_CONFIG, CONFIG_MAX_SLOTS_EN(max_slots));

  // set up the device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * max_slots;
  void *dcbaap = kmalloca(dcbaap_size, 64);
  memset(dcbaap, 0, dcbaap_size);
  hc->dcbaap = dcbaap;
  uint64_t dcbaap_ptr = kheap_ptr_to_phys(dcbaap);
  write64(hc->op_base, XHCI_OP_DCBAAP, DCBAAP_PTR(dcbaap_ptr));

  // set up the command ring
  hc->cmd_ring = _xhci_alloc_ring(CMD_RING_SIZE);
  uint64_t crcr = CRCR_PTR(_xhci_ring_device_ptr(hc->cmd_ring));
  if (hc->cmd_ring->cycle) {
    crcr |= CRCR_RCS;
  }
  write64(hc->op_base, XHCI_OP_CRCR, crcr);

  // set up the root interrupter
  hc->interrupters = _xhci_setup_interrupter(hc, xhci_host_irq_handler, hc);
  hc->evt_ring = hc->interrupters->ring;

  // event thread
  kprintf("xhci: creating event thread\n");
  mutex_init(&hc->lock, 0);
  hc->thread = thread_create(_xhci_controller_event_loop, hc);
  thread_yield();

  // run the controller
  uint32_t usbcmd = read32(hc->op_base, XHCI_OP_USBCMD);
  usbcmd |= USBCMD_RUN | USBCMD_INT_EN | USBCMD_HS_ERR_EN;
  write32(hc->op_base, XHCI_OP_USBCMD, usbcmd);
  return 0;
}

int _xhci_halt_controller(xhci_controller_t *hc) {
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

xhci_interrupter_t *_xhci_setup_interrupter(xhci_controller_t *hc, irq_handler_t fn, void *data) {
  uint8_t n = atomic_fetch_add(&hc->max_intr, 1);

  int irq = irq_alloc_software_irqnum();
  kassert(irq >= 0);
  irq_register_irq_handler(irq, fn, data);
  irq_enable_msi_interrupt(irq, n, hc->pcie_device);

  size_t erst_size = sizeof(xhci_erst_entry_t) * 1;
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  _xhci_ring_t *ring = _xhci_alloc_ring(EVT_RING_SIZE);
  erst[0].rs_addr = _xhci_ring_device_ptr(ring);
  erst[0].rs_size = _xhci_ring_size(ring);

  xhci_interrupter_t *intr = kmalloc(sizeof(xhci_interrupter_t));
  intr->index = n;
  intr->vector = irq;
  intr->ring = ring;
  intr->erst = (uintptr_t) erst;
  LIST_ENTRY_INIT(&intr->list);

  uintptr_t erstba_ptr = kheap_ptr_to_phys(erst);
  uintptr_t erdp_ptr = PAGE_PHYS_ADDR(ring->page);
  write32(hc->rt_base, XHCI_INTR_IMOD(n), IMOD_INTERVAL(4000));
  write32(hc->rt_base, XHCI_INTR_ERSTSZ(n), ERSTSZ(erst_size / sizeof(xhci_erst_entry_t)));
  write64(hc->rt_base, XHCI_INTR_ERSTBA(n), ERSTBA_PTR(erstba_ptr));
  write64(hc->rt_base, XHCI_INTR_ERDP(n), ERDP_PTR(erdp_ptr));

  uint32_t iman = read32(hc->rt_base, XHCI_INTR_IMAN(n));
  iman |= IMAN_IE;
  write32(hc->rt_base, XHCI_INTR_IMAN(n), iman);
  return intr;
}

_xhci_port_t *_xhci_discover_ports(xhci_controller_t *hc) {
  kprintf("xhci: discovering ports\n");
  LIST_HEAD(_xhci_port_t) ports = LIST_HEAD_INITR;

  _xhci_protocol_t *protocol = NULL;
  RLIST_FOREACH(protocol, hc->protocols, list) {
    uint8_t offset = protocol->port_offset;
    uint8_t count = protocol->port_count;
    for (int i = offset; i < offset + count; i++) {
      uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(i - 1));
      if (!(portsc & PORTSC_CCS)) {
        continue; // no device connected
      }

      kprintf("xhci: device found on port %d\n", i);

      _xhci_port_t *port = kmalloc(sizeof(_xhci_port_t));
      port->number = i;
      port->protocol = protocol;
      port->speed = PORTSC_SPEED(portsc);
      port->device = NULL;
      LIST_ADD(&ports, port, list);
    }
  }

  return LIST_FIRST(&ports);
}

int _xhci_enable_port(xhci_controller_t *hc, _xhci_port_t *port) {
  uint8_t n = port->number - 1;
  uint32_t portsc = read32(hc->op_base, XHCI_PORT_SC(n));
  if (port->protocol->rev_major == 0x3) {
    // USB3
    portsc |= PORTSC_WARM_RESET;
  } else if (port->protocol->rev_major == 0x2) {
    // USB2
    portsc |= PORTSC_RESET;
  } else {
    kprintf("xhci: port %d: invalid usb revision\n", port->number);
    return -1;
  }
  write32(hc->op_base, XHCI_PORT_SC(n), portsc);

  while ((read32(hc->op_base, XHCI_PORT_SC(n)) & PORTSC_EN) == 0) {
    cpu_pause();
  }
  return 0;
}

//

void *_xhci_get_cap(xhci_controller_t *hc, uint8_t cap_id, void *last_cap) {
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

_xhci_protocol_t *_xhci_get_protocols(xhci_controller_t *hc) {
  LIST_HEAD(_xhci_protocol_t) protocols = LIST_HEAD_INITR;

  uint32_t *cap = NULL;
  while (true) {
    cap = _xhci_get_cap(hc, XHCI_CAP_PROTOCOL, cap);
    if (cap == NULL) {
      break;
    }

    uint8_t rev_minor = (cap[0] >> 16) & 0xFF;
    uint8_t rev_major = (cap[0] >> 24) & 0xFF;
    uint8_t port_offset = cap[2] & 0xFF;
    uint8_t port_count = (cap[2] >> 8) & 0xFF;
    uint8_t slot_type = cap[3] & 0x1F;

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

//
// MARK: Devices
//

_xhci_device_t *_xhci_setup_device(xhci_controller_t *hc, _xhci_port_t *port, uint8_t slot) {
  // device struct
  _xhci_device_t *device = kmalloc(sizeof(_xhci_device_t));
  device->host = hc;
  device->port = port;

  device->slot_id = slot;
  device->dev_class = 0;
  device->dev_subclass = 0;
  device->dev_protocol = 0;

  device->ictx = _xhci_alloc_input_ctx(device);
  device->dctx = _xhci_alloc_device_ctx(device);
  device->xfer_ring = device->ictx->ctrl_ring;

  device->desc = NULL;
  device->configs = NULL;
  device->endpoints = NULL;
  device->interrupters = _xhci_setup_interrupter(hc, xhci_device_irq_handler, device);
  device->evt_ring = device->interrupters->ring;

  device->ictx->slot->intrptr_target = device->interrupters->index;

  clear_trb(&device->xfer_evt_trb);
  cond_init(&device->xfer_evt_cond, 0);
  LIST_ENTRY_INIT(&device->list);

  mutex_init(&device->lock, 0);
  device->thread = thread_create(_xhci_device_event_loop, device);
  thread_yield();

  return device;
}

xhci_endpoint_t *_xhci_setup_device_ep(_xhci_device_t *device, usb_ep_descriptor_t *desc) {
  uint8_t ep_num = desc->ep_addr & 0xF;
  uint8_t ep_dir = (desc->ep_addr >> 7) & 0x1;
  uint8_t ep_type = desc->attributes & 0x3;
  uint8_t index = ep_index(ep_num, ep_dir);

  // endpoint struct
  xhci_endpoint_t *ep = kmalloc(sizeof(xhci_endpoint_t));
  ep->number = ep_num;
  ep->index = index;
  ep->type = get_ep_type(ep_type, ep_dir);
  ep->dir = ep_dir;
  ep->ctx = device->ictx->endpoint[index];
  ep->ring = _xhci_alloc_ring(XFER_RING_SIZE);
  memset(&ep->last_event, 0, sizeof(usb_event_t));
  LIST_ENTRY_INIT(&ep->list);

  kprintf("xhci: endpoint %d | %s\n", ep_num, get_speed_str(device->port->speed));

  xhci_endpoint_ctx_t *ctx = ep->ctx;
  ctx->ep_type = get_ep_type(ep_type, ep_dir);
  ctx->interval = desc->interval;
  ctx->max_packt_sz = desc->max_pckt_sz;
  ctx->max_burst_sz = 0;
  ctx->tr_dequeue_ptr = _xhci_ring_device_ptr(ep->ring) | 1;
  ctx->avg_trb_length = 8;
  ctx->max_streams = 0;
  ctx->mult = 0;
  ctx->cerr = 3;

  RLIST_ADD_FRONT(&device->endpoints, ep, list);
  return ep;
}

usb_config_descriptor_t *_xhci_get_device_configs(_xhci_device_t *device) {
  kassert(device->desc != NULL);
  usb_device_descriptor_t *desc = device->desc;
  usb_config_descriptor_t *configs = kmalloc(desc->num_configs * sizeof(usb_config_descriptor_t));
  device->configs = configs;
  for (int i = 0; i < desc->num_configs; i++) {
    size_t size;
    usb_config_descriptor_t *config = _xhci_get_device_descriptor(device, CONFIG_DESCRIPTOR, i, &size);
    if (config == NULL) {
      kprintf("xhci: failed to get config descriptor (%d)\n", i);
      return NULL;
    } else if (config->type != CONFIG_DESCRIPTOR) {
      kprintf("xhci: unexpected descriptor type\n");
      kfree(config);
      return NULL;
    }

    memcpy(&configs[i], config, sizeof(usb_config_descriptor_t));
    kfree(config);
  }
  return configs;
}

int _xhci_select_device_config(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  xhci_input_ctx_t *input = device->ictx;
  usb_device_descriptor_t *desc = device->desc;

  for (int i = 0; i < desc->num_configs; i++) {
  label(try_config);

    usb_config_descriptor_t *config = &device->configs[i];
    usb_if_descriptor_t *interfaces = ptr_after(config);

    char *config_str = _xhci_get_string_descriptor(device, config->this_idx);
    kprintf("xhci: config: %s\n", config_str);
    kfree(config_str);

    for (int j = 0; j < config->num_ifs; j++) {
      usb_if_descriptor_t *interface = &interfaces[j];

      uint8_t class_code = desc->dev_class;
      uint8_t subclass_code = desc->dev_subclass;
      uint8_t protocol = desc->dev_protocol;
      if (class_code == USB_CLASS_NONE) {
        class_code = interface->if_class;
        subclass_code = interface->if_subclass;
        protocol = interface->if_protocol;
      }

      kprintf("xhci: trying config %d interface %d\n", i, j);
      kprintf("      class: %d | subclass: %d | protocol: %d\n", class_code, subclass_code, protocol);

      for (int k = 0; k < interface->num_eps; k++) {
        usb_ep_descriptor_t *endpoint = usb_get_ep_descriptor(interface, k);

        xhci_endpoint_t *ep = _xhci_setup_device_ep(device, endpoint);
        input->slot.ctx_entries++;
        input->ctrl.drop_flags = 0;
        input->ctrl.add_flags = 1 | (1 << (ep->index + 1));
        if (_xhci_run_configure_ep_cmd(hc, device) < 0) {
          kprintf("xhci: failed to configure endpoint\n");
          kfree(ep);
          goto try_config;
        }
      }

      usb_setup_packet_t get_desc = SET_CONFIGURATION(config->config_val);
      _xhci_queue_setup(device, get_desc, SETUP_DATA_NONE);
      _xhci_queue_status(device, DATA_OUT);
      if (_xhci_await_transfer(device, NULL) < 0) {
        kprintf("xhci: failed to get set config\n");
        return -1;
      }

      kprintf("xhci: device configured with config %d interface %d\n", i, j);
      device->dev_class = class_code;
      device->dev_subclass = subclass_code;
      device->dev_protocol = protocol;
      device->dev_config = i;
      device->dev_if = j;
      return 0;
    }
  }

  return -EFAILED;
}

xhci_endpoint_t *_xhci_get_device_ep(_xhci_device_t *device, uint8_t ep_num) {
  xhci_endpoint_t *ep;
  RLIST_FOREACH(ep, device->endpoints, list) {
    if (ep->number == ep_num) {
      return ep;
    }
  }
  return NULL;
}

xhci_endpoint_t *_xhci_find_device_ep(_xhci_device_t *device, bool direction) {
  xhci_endpoint_t *ep;
  RLIST_FOREACH(ep, device->endpoints, list) {
    switch (ep->ctx->ep_type) {
      case XHCI_ISOCH_OUT_EP:
      case XHCI_BULK_OUT_EP:
      case XHCI_INTR_OUT_EP:
        if (direction == DATA_OUT) {
          return ep;
        }
        break;
      case XHCI_ISOCH_IN_EP:
      case XHCI_BULK_IN_EP:
      case XHCI_INTR_IN_EP:
        if (direction == DATA_IN) {
          return ep;
        }
        break;
      default:
        break;
    }
  }
  return NULL;
}

// descriptors

void *_xhci_get_device_descriptor(_xhci_device_t *device, uint8_t type, uint8_t index, size_t *bufsize) {
  size_t size = 8;
LABEL(make_request);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(type, index, size);
  uint8_t *desc = kmalloc(size);
  memset(desc, 0, size);

  _xhci_queue_setup(device, get_desc, SETUP_DATA_IN);
  _xhci_queue_data(device, (uintptr_t) desc, size, DATA_IN);
  _xhci_queue_status(device, DATA_OUT);
  if (_xhci_await_transfer(device, NULL) < 0) {
    kprintf("xhci: failed to get device descriptor\n");
    return NULL;
  }

  size_t new_size;
  if (type == CONFIG_DESCRIPTOR) {
    uint16_t *buf = (void *) desc;
    new_size = buf[1];
  } else {
    new_size = desc[0];
  }

  if (new_size > size) {
    size = new_size;
    kfree(desc);
    goto make_request;
  }

  if (bufsize != NULL) {
    *bufsize = size;
  }
  return desc;
}

char *_xhci_get_string_descriptor(_xhci_device_t *device, uint8_t index) {
  // allocate enough to handle most cases
  size_t size = 64;
label(make_request);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(STRING_DESCRIPTOR, index, size);
  usb_string_t *string = kmalloc(size);
  _xhci_queue_setup(device, get_desc, SETUP_DATA_IN);
  _xhci_queue_data(device, (uintptr_t) string, size, DATA_IN);
  _xhci_queue_status(device, DATA_OUT);
  if (_xhci_await_transfer(device, NULL) < 0) {
    kprintf("xhci: failed to get string descriptor\n");
    return NULL;
  }

  if (string->length > size) {
    size = string->length;
    kfree(string);
    goto make_request;
  }

  // number of utf16 characters is string->length - 2
  // and since we're converting to null-terminated ascii
  // we only need half as many characters.
  size_t len = ((string->length - 2) / 2);
  char *ascii = kmalloc(len + 1); // 1 extra null char
  ascii[len] = 0;

  int result = utf16_iconvn_ascii(ascii, string->string, len);
  if (result != len) {
    kprintf("xhci: string descriptor conversion failed\n");
  }

  kfree(string);
  return ascii;
}

//
// MARK: Commannds
//

int _xhci_run_command_trb(xhci_controller_t *hc, xhci_trb_t trb, xhci_trb_t *result) {
  kprintf("xhci: running command [type = %d]\n", trb.trb_type);
  _xhci_ring_enqueue_trb(hc->cmd_ring, trb);

  // ring the host doorbell
  write32(hc->db_base, XHCI_DB(0), DB_TARGET(0));

  if (cond_wait(&hc->cmd_compl_cond) < 0) {
    kprintf("xhci: failed to wait for event\n");
    return -1;
  }

  kprintf("xhci: event received\n");
  xhci_cmd_compl_evt_trb_t evt_trb = downcast_trb(&hc->cmd_compl_trb, xhci_cmd_compl_evt_trb_t);
  if (result != NULL) {
    *result = cast_trb(&evt_trb);
  }
  return evt_trb.compl_code == CC_SUCCESS;
}

int _xhci_run_noop_cmd(xhci_controller_t *hc) {
  kprintf("xhci: running no-op command\n");

  xhci_noop_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_NOOP_CMD;

  xhci_cmd_compl_evt_trb_t result;
  if (_xhci_run_command_trb(hc, cast_trb(&cmd), upcast_trb_ptr(&result)) < 0) {
    return -1;
  }

  kprintf("xhci: no-op success\n");
  return 0;
}

int _xhci_run_enable_slot_cmd(xhci_controller_t *hc, _xhci_port_t *port) {
  kprintf("xhci: enabling slot\n");

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
  kprintf("xhci: addressing device\n");

  xhci_addr_dev_cmd_trb_t cmd;
  clear_trb(&cmd);
  cmd.trb_type = TRB_ADDR_DEV_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = PAGE_PHYS_ADDR(device->ictx->pages);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

int _xhci_run_configure_ep_cmd(xhci_controller_t *hc, _xhci_device_t *device) {
  kprintf("xhci: configuring endpoint\n");

  xhci_config_ep_cmd_trb_t cmd;
  cmd.trb_type = TRB_CONFIG_EP_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = PAGE_PHYS_ADDR(device->ictx->pages);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

int _xhci_run_evaluate_ctx_cmd(xhci_controller_t *hc, _xhci_device_t *device) {
  kprintf("xhci: evaluating context\n");

  xhci_eval_ctx_cmd_trb_t cmd;
  cmd.trb_type = TRB_EVAL_CTX_CMD;
  cmd.slot_id = device->slot_id;
  cmd.input_ctx = PAGE_PHYS_ADDR(device->ictx->pages);
  return _xhci_run_command_trb(hc, cast_trb(&cmd), NULL);
}

//
// MARK: Transfers
//

int _xhci_queue_setup(_xhci_device_t *device, usb_setup_packet_t setup, uint8_t type) {
  if (type != SETUP_DATA_NONE && type != SETUP_DATA_OUT && type != SETUP_DATA_IN) {
    return -EINVAL;
  }

  xhci_setup_trb_t trb;
  clear_trb(&trb);
  memcpy(&trb, &setup, sizeof(usb_setup_packet_t));
  trb.trb_type = TRB_SETUP_STAGE;
  trb.trs_length = 8;
  trb.tns_type = type;
  trb.intr_trgt = trb.intr_trgt = device->interrupters->index;;
  trb.ioc = 1;
  trb.idt = 1;

  _xhci_ring_enqueue_trb(device->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_data(_xhci_device_t *device, uintptr_t buffer, uint16_t size, bool direction) {
  xhci_data_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_DATA_STAGE;
  trb.buf_ptr = _vm_virt_to_phys(buffer);
  trb.trs_length = size;
  trb.td_size = 0;
  trb.intr_trgt = device->interrupters->index;
  trb.dir = direction;

  _xhci_ring_enqueue_trb(device->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_status(_xhci_device_t *device, bool direction) {
  xhci_status_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_STATUS_STAGE;
  trb.intr_trgt = device->interrupters->index;
  trb.dir = direction;
  trb.ioc = 1;

  _xhci_ring_enqueue_trb(device->xfer_ring, cast_trb(&trb));
  return 0;
}

int _xhci_queue_transfer(_xhci_device_t *device, uintptr_t buffer, uint16_t size, bool direction, uint8_t flags) {
  xhci_normal_trb_t trb;
  clear_trb(&trb);
  trb.trb_type = TRB_NORMAL;
  trb.buf_ptr = buffer;
  trb.trs_length = size;
  trb.intr_trgt = device->interrupters->index;
  trb.ns = (flags & XHCI_XFER_NS) != 0;
  trb.isp = (flags & XHCI_XFER_ISP) != 0;
  trb.ioc = (flags & XHCI_XFER_IOC) != 0;

  xhci_endpoint_t *ep = _xhci_find_device_ep(device, direction);
  if (ep == NULL) {
    return -EINVAL;
  }

  _xhci_ring_enqueue_trb(ep->ring, cast_trb(&trb));
  return 0;
}

int _xhci_await_transfer(_xhci_device_t *device, xhci_trb_t *result) {
  xhci_controller_t *hc = device->host;

  // ring the slot doorbell
  write32(hc->db_base, XHCI_DB(device->slot_id), DB_TARGET(1));
  if (cond_wait(&device->xfer_evt_cond) < 0) {
    kprintf("xhci: failed to wait for event\n");
    return -1;
  }

  kprintf("xhci: event received\n");
  xhci_transfer_evt_trb_t evt_trb = downcast_trb(&device->xfer_evt_trb, xhci_transfer_evt_trb_t);
  kassert(evt_trb.trb_type == TRB_TRANSFER_EVT);
  if (result != NULL) {
    *result = cast_trb(&evt_trb);
  }
  return evt_trb.compl_code == CC_SUCCESS;
}

//
// MARK: Contexts
//

xhci_ictx_t *_xhci_alloc_input_ctx(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  page_t *page = valloc_zero_pages(1, PG_WRITE | PG_NOCACHE);
  void *ptr = (void *) PAGE_VIRT_ADDR(page);

  xhci_ictx_t *ictx = kmalloc(sizeof(xhci_ictx_t));
  memset(ictx, 0, sizeof(xhci_ictx_t));
  ictx->pages = page;
  ictx->ctrl = ptr;
  ictx->slot = offset_ptr(ptr, ctxsz);
  for (int i = 0; i < 31; i++) {
    ictx->endpoint[i] = offset_ptr(ptr, ctxsz * (i + 2));
  }

  xhci_input_ctrl_ctx_t *ctrl_ctx = ictx->ctrl;
  // set A0 and A1 flags to 1
  ctrl_ctx->add_flags |= 0x3;

  xhci_slot_ctx_t *slot_ctx = ictx->slot;
  slot_ctx->root_hub_port = device->port->number;
  slot_ctx->route_string = 0;
  slot_ctx->speed = device->port->speed;
  slot_ctx->ctx_entries = 1;

  // allocate transfer ring
  _xhci_ring_t *xfer_ring = _xhci_alloc_ring(XFER_RING_SIZE);
  ictx->ctrl_ring = xfer_ring;

  // endpoint 0 (default control endpoint) context
  xhci_endpoint_ctx_t *ep_ctx = ictx->endpoint[0];
  ep_ctx->ep_type = XHCI_CTRL_BI_EP;
  ep_ctx->max_packt_sz = 64;
  ep_ctx->max_burst_sz = 0;
  ep_ctx->cerr = 3;
  ep_ctx->tr_dequeue_ptr = _xhci_ring_device_ptr(xfer_ring) | xfer_ring->cycle;
  ep_ctx->avg_trb_length = 8;
  ep_ctx->interval = 0;
  ep_ctx->max_streams = 0;
  ep_ctx->mult = 0;

  return ictx;
}

xhci_dctx_t *_xhci_alloc_device_ctx(_xhci_device_t *device) {
  xhci_controller_t *hc = device->host;
  size_t ctxsz = is_64_byte_context(hc) ? 64 : 32;

  // input context
  page_t *page = valloc_zero_pages(1, PG_WRITE | PG_NOCACHE);
  void *ptr = (void *) PAGE_VIRT_ADDR(page);

  xhci_dctx_t *dctx = kmalloc(sizeof(xhci_dctx_t));
  memset(dctx, 0, sizeof(xhci_dctx_t));
  dctx->pages = page;
  dctx->slot = ptr;
  for (int i = 0; i < 31; i++) {
    dctx->endpoint[i] = offset_ptr(ptr, ctxsz * (i + 2));
  }

  return dctx;
}

//
// MARK: TRB Rings
//

_xhci_ring_t *_xhci_alloc_ring(size_t capacity) {
  size_t num_pages = SIZE_TO_PAGES(capacity * sizeof(xhci_trb_t));
  size_t num_trbs = PAGES_TO_SIZE(num_pages) / sizeof(xhci_trb_t);

  _xhci_ring_t *ring = kmalloc(sizeof(_xhci_ring_t));
  memset(ring, 0, sizeof(_xhci_ring_t));
  ring->page = valloc_zero_pages(num_pages, PG_WRITE);
  ring->ptr = (void *) PAGE_VIRT_ADDR(ring->page);
  ring->index = 0;
  ring->max_index = num_trbs;
  ring->cycle = 1;
  return ring;
}

void _xhci_free_ring(_xhci_ring_t *ring) {
  vfree_pages(ring->page);
  kfree(ring);
}

int _xhci_ring_enqueue_trb(_xhci_ring_t *ring, xhci_trb_t trb) {
  kassert(trb.trb_type != 0);
  trb.cycle = ring->cycle;
  ring->ptr[ring->index] = trb;
  ring->index++;

  if (ring->index == ring->max_index - 1) {
    xhci_link_trb_t link;
    clear_trb(&link);
    link.trb_type = TRB_LINK;
    link.cycle = ring->cycle;
    link.toggle_cycle = 1;
    link.rs_addr = PAGE_PHYS_ADDR(ring->page);
    ring->ptr[ring->index] = cast_trb(&link);

    ring->index = 0;
    ring->cycle = !ring->cycle;
  }
  return 0;
}

bool _xhci_ring_dequeue_trb(_xhci_ring_t *ring, xhci_trb_t *out) {
  kassert(out != NULL);
  xhci_trb_t trb = ring->ptr[ring->index];
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
  return PAGE_PHYS_ADDR(ring->page) + (ring->index * sizeof(xhci_trb_t));
}

size_t _xhci_ring_size(_xhci_ring_t *ring) {
  return ring->max_index * sizeof(xhci_trb_t);
}

//
//
//
//
//
//

// MARK: Old Code

void xhci_init() {
  xhci_log("initializing controller");

  pcie_device_t *device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (device == NULL) {
    xhci_log("no xhci controller");
    return;
  }

  xhci_dev_t *xhci = kmalloc(sizeof(xhci_dev_t));
  pcie_bar_t *bar = pcie_get_bar(device, 0);
  uintptr_t phys_addr = bar->phys_addr;
  size_t size = bar->size;

  // map xhci registers
  uintptr_t virt_addr = (uintptr_t) _vmap_mmio(phys_addr, size, PG_WRITE | PG_NOCACHE);
  bar->virt_addr = virt_addr;

  xhci->pci_dev = device;
  xhci->phys_addr = phys_addr;
  xhci->virt_addr = virt_addr;
  xhci->size = bar->size;

  xhci->cap_base = virt_addr;
  xhci->op_base = virt_addr + xhci_cap->length;
  xhci->rt_base = virt_addr + xhci_cap->rtsoff;
  xhci->db_base = virt_addr + xhci_cap->dboff;
  xhci->xcap_base = virt_addr + (xhci_cap->hccparams1.ext_cap_ptr << 2);

  cond_init(&xhci->init, 0);
  cond_init(&xhci->event, 0);
  cond_init(&xhci->event_ack, 0);

  // spawn threads
  xhci->event_thread = thread_create(xhci_event_loop, xhci);
  thread_setsched(xhci->event_thread, POLICY_DRIVER, 255);
  thread_yield();

  // initialize the controller
  if (xhci_init_controller(xhci) != 0) {
    panic("[xhci] failed to initialize controller");
  }
  xhc = xhci;

  // get protocols
  xhci->protocols = xhci_get_protocols(xhci);
  // xhci->speeds = xhci_get_speeds(xhci);
  xhci->speeds = NULL;

  // discover ports
  xhci->ports = xhci_discover_ports(xhci);
  xhci_log("done!");
}

void xhci_setup_devices() {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  // setup devices
  xhci_port_t *port = xhci->ports;
  while (port) {
    // if (port->number == 5) {
    //   port = port->next;
    //   continue;
    // }

    kprintf("port: %d\n", port->number);
    xhci_trace_debug("setting up device on port %d", port->number);
    xhci_trace_debug("port speed: %s", get_speed_str(port->speed));

    xhci_trace_debug("enabling port %d", port->number);
    if (xhci_enable_port(xhci, port) != 0) {
      xhci_trace_debug("failed to enable port %d", port->number);
      port = port->next;
      continue;
    }
    xhci_trace_debug("enabled port %d", port->number);

    xhci_trace_debug("enabling slot");
    int result = xhci_enable_slot(xhci);
    if (result < 0) {
      xhci_trace_debug("failed to enable slot");
      port = port->next;
      continue;
    }
    xhci_trace_debug("enabled slot");

    xhci_device_t *device = xhci_alloc_device(xhci, port, result);
    if (device == NULL) {
      xhci_trace_debug("failed to setup device on port %d", port->number);
      port = port->next;
      continue;
    }
    port->device = device;

    xhci_trace_debug("addressing device");
    if (xhci_address_device(xhci, device) != 0) {
      xhci_trace_debug("failed to address device");
      port = port->next;
      continue;
    }
    xhci_trace_debug("addressed device!");

    xhci_trace_debug("reading device descriptor");
    usb_device_descriptor_t *desc = xhci_get_descriptor(device, DEVICE_DESCRIPTOR, 0, NULL);
    if (desc == NULL) {
      xhci_trace_debug("failed to get device descriptor");
      port = port->next;
      continue;
    }
    xhci_trace_debug("found device descriptor!");

    device->desc = desc;

    xhci_input_ctx_t *input = device->ictx;
    input->ctrl.add_flags |= 1;
    input->endpoint[0].max_packt_sz = desc->max_packt_sz0;
    xhci_trace_debug("evaluating context");
    if (xhci_evaluate_context(xhci, device) != 0) {
      xhci_trace_debug("failed to evaluate context");
      port = port->next;
      continue;
    }
    xhci_trace_debug("context evaluated!");

    xhci_trace_debug("reading device configs");
    if (xhci_get_device_configs(device) != 0) {
      xhci_trace_debug("failed to read configs");
      port = port->next;
      continue;
    }
    xhci_trace_debug("configs read!");

    xhci_trace_debug("selecting config");
    if (xhci_select_device_config(device) != 0) {
      xhci_trace_debug("failed to select config");
      port = port->next;
      continue;
    }
    xhci_trace_debug("config selected!");

    device->thread = thread_create(xhci_device_event_loop, device);
    thread_setsched(device->thread, POLICY_DRIVER, 255);
    thread_yield();

    xhci_trace_debug("device enabled!");
    usb_register_device(device);
    port = port->next;
  }
}

int xhci_init_controller(xhci_dev_t *xhci) {
  // take ownership of xhc if needed
  xhci_cap_t *cap_ptr = NULL;
  cap_ptr = xhci_get_cap(xhci, cap_ptr, XHCI_CAP_LEGACY);
  if (cap_ptr != NULL) {
    xhci_trace_debug("taking ownership");
    xhci_cap_legacy_t *legacy = (void *) cap_ptr;
    legacy->os_sem = 1;

    WAIT_TIMEOUT(legacy->os_sem == 1, return ETIMEDOUT);
    xhci_trace_debug("took ownership");
  }

  // reset the controller
  xhci_trace_debug("reseting controller");

  xhci_op->usbcmd.run = 0;
  xhci_op->usbcmd.hc_reset = 1;
  WAIT_TIMEOUT(xhci_op->usbsts_r & USBSTS_NOT_READY, return ETIMEDOUT);

  uint8_t max_ports = xhci_cap->hcsparams1.max_ports;
  uint8_t max_slots = xhci_cap->hcsparams1.max_slots;
  xhci_op->config.max_slots_en = max_slots;
  xhci_trace_debug("number of ports: %d", max_ports);
  xhci_trace_debug("number of slots: %d", max_slots);

  // set up the device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * max_slots;
  void *dcbaap = kmalloca(dcbaap_size, 64);
  xhci->dcbaap = dcbaap;
  xhci_op->dcbaap = kheap_ptr_to_phys(dcbaap);

  // set up the command ring
  xhci_ring_t *ring = xhci_alloc_ring();
  xhci->cmd_ring = ring;
  xhci_op->crcr_r |= PAGE_PHYS_ADDR(ring->page) | CRCR_RCS;

  // set up the root interrupter
  xhci_intr_t *intrptr = xhci_setup_interrupter(xhci, irq_callback, xhci);
  xhci->intr = intrptr;

  // run the controller
  xhci_log("starting controller");
  xhci_op->usbcmd_r |= USBCMD_RUN | USBCMD_INT_EN | USBCMD_HS_ERR_EN;

  WAIT_READY(return -ETIMEDOUT);
  return 0;
}

xhci_intr_t *xhci_setup_interrupter(xhci_dev_t *xhci, irq_handler_t fn, void *data) {
  uint8_t n = atomic_fetch_add(&intr_number, 1);

  int irq = irq_alloc_software_irqnum();
  kassert(irq >= 0);
  irq_register_irq_handler(irq, fn, data);
  irq_enable_msi_interrupt(irq, n, xhci->pci_dev);

  size_t erst_size = sizeof(xhci_erst_entry_t) * 1;
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  xhci_ring_t *ring = xhci_alloc_ring();
  erst[0].rs_addr = PAGE_PHYS_ADDR(ring->page);
  erst[0].rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  xhci_intr_t *intr = kmalloc(sizeof(xhci_intr_t));
  intr->vector = irq;
  intr->number = n;
  intr->ring = ring;
  intr->erst = (uintptr_t) erst;

  xhci_intr(n)->imod.imodi = 4000;
  xhci_intr(n)->erstsz = 1;
  xhci_intr(n)->erstba_r = kheap_ptr_to_phys(erst);
  xhci_intr(n)->erdp_r = PAGE_PHYS_ADDR(ring->page);
  xhci_intr(n)->iman.ie = 1;
  return intr;
}

void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint) {
  xhci_trace_debug("ringing doorbell %d:%d", slot, endpoint);
  xhci_db(slot)->target = endpoint;
}

xhci_cap_t *xhci_get_cap(xhci_dev_t *xhci, xhci_cap_t *cap_ptr, uint8_t cap_id) {
  if (cap_ptr == NULL) {
    cap_ptr = (void *) xhci->xcap_base;
  } else if (cap_ptr->next == 0) {
    return NULL;
  } else {
    cap_ptr = offset_ptr(cap_ptr, cap_ptr->next << 2);
  }

  while (true) {
    if (cap_ptr->id == cap_id) {
      return cap_ptr;
    }

    if (cap_ptr->next == 0) {
      return NULL;
    }
    cap_ptr = offset_ptr(cap_ptr, cap_ptr->next << 2);
  }
}

xhci_protocol_t *xhci_get_protocols(xhci_dev_t *xhci) {
  xhci_protocol_t *first = NULL;
  xhci_protocol_t *protos = NULL;

  xhci_cap_t *cap_ptr = NULL;
  while (true) {
    cap_ptr = xhci_get_cap(xhci, cap_ptr, XHCI_CAP_PROTOCOL);
    if (cap_ptr == NULL) {
      break;
    }

    xhci_cap_protocol_t *p = (void *) cap_ptr;
    xhci_protocol_t *proto = kmalloc(sizeof(xhci_protocol_t));

    proto->rev_major = p->rev_major;
    proto->rev_minor = p->rev_minor;
    proto->port_offset = p->port_offset;
    proto->port_count = p->port_count;
    proto->next = NULL;

    if (protos != NULL) {
      protos->next = proto;
    } else {
      first = proto;
    }
    protos = proto;
  }
  return first;
}

xhci_speed_t *xhci_get_speeds(xhci_dev_t *xhci) {
  xhci_speed_t *first = NULL;
  xhci_speed_t *speeds = NULL;

  xhci_cap_t *cap_ptr = NULL;
  while (true) {
    cap_ptr = xhci_get_cap(xhci, cap_ptr, XHCI_CAP_PROTOCOL);
    if (cap_ptr == NULL) {
      break;
    }

    xhci_cap_protocol_t *p = (void *) cap_ptr;
    xhci_speed_t *speed = kmalloc(sizeof(xhci_speed_t));

    xhci_port_speed_t *ps = offset_ptr(p, XHCI_PSI_OFFSET);

    if (speeds != NULL) {
      speeds->next = speed;
    } else {
      first = speed;
    }
    speeds = speed;
  }
  return first;
}

//
// MARK: Ports
//

xhci_port_t *xhci_discover_ports(xhci_dev_t *xhci) {
  xhci_trace_debug("discovering ports");

  xhci_port_t *ports = NULL;
  xhci_port_t *last = NULL;

  xhci_protocol_t *proto = xhci->protocols;
  while (proto) {
    xhci_trace_debug("USB %x.%x supported", proto->rev_major, proto->rev_minor);
    for (int i = proto->port_offset; i < proto->port_offset + proto->port_count; i++) {
      if (!(xhci_port(i - 1)->portsc.ccs)) {
        continue; // no device is connected
      }

      xhci_trace_debug("found device on port %d", i);
      xhci_port_t *p = kmalloc(sizeof(xhci_port_t));
      p->number = i;
      p->protocol = proto;
      p->speed = xhci_port(i)->portsc.speed;
      p->device = NULL;
      p->next = NULL;
      if (last != NULL) {
        last->next = p;
      } else {
        ports = p;
      }
      last = p;
    }
    proto = proto->next;
  }
  return ports;
}

int xhci_enable_port(xhci_dev_t *xhci, xhci_port_t *port) {
  uint8_t n = port->number - 1;
  if (port->protocol->rev_major == 0x3) {
    // USB 3
    xhci_port(n)->portsc.warm_rst = 1;
  } else if (port->protocol->rev_major == 0x2) {
    // USB 2
    xhci_port(n)->portsc.reset = 1;
  }

  WAIT_TIMEOUT(!(xhci_port(n)->portsc.enabled), return ETIMEDOUT);

  if (port->protocol->rev_major == 0x3) {
    xhci_port(n)->portsc.wrc = 1;
  } else if (port->protocol->rev_major == 0x2) {
    xhci_port(n)->portsc.prc = 1;
  }
  xhci_port(n)->portsc.csc = 1;
  return 0;
}

//
// MARK: Commands
//

void *xhci_run_command(xhci_dev_t *xhci, xhci_trb_t *trb) {
  cond_clear_signal(&xhci->event_ack);
  xhci_ring_enqueue_trb(xhci->cmd_ring, trb);
  xhci_ring_db(xhci, 0, 0);

  cond_wait(&xhci->event_ack);
  return xhci->event_thread->data;
}

int xhci_enable_slot(xhci_dev_t *xhci) {
  // allocate slot for port
  xhci_enabl_slot_cmd_trb_t enbl_cmd = { 0 };
  enbl_cmd.trb_type = TRB_ENABL_SLOT_CMD;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_run_command(xhci, as_trb(enbl_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != CC_SUCCESS) {
    return -EFAILED;
  }
  return result->slot_id;
}

// disable slot

int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device) {
  xhci_addr_dev_cmd_trb_t addr_cmd = { 0 };
  addr_cmd.input_ctx = PAGE_PHYS_ADDR(device->pages);
  addr_cmd.trb_type = TRB_ADDR_DEV_CMD;
  addr_cmd.slot_id = device->slot_id;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_run_command(xhci, as_trb(addr_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != CC_SUCCESS) {
    return -EFAILED;
  }

  return 0;
}

int xhci_configure_endpoint(xhci_dev_t *xhci, xhci_device_t *device) {
  xhci_config_ep_cmd_trb_t config_cmd = { 0 };
  config_cmd.input_ctx = PAGE_PHYS_ADDR(device->pages);
  config_cmd.trb_type = TRB_CONFIG_EP_CMD;
  config_cmd.slot_id = device->slot_id;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_run_command(xhci, as_trb(config_cmd));

  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != CC_SUCCESS) {
    return -EFAILED;
  }

  return 0;
}

int xhci_evaluate_context(xhci_dev_t *xhci, xhci_device_t *device) {
  xhci_eval_ctx_cmd_trb_t eval_cmd = { 0 };
  eval_cmd.input_ctx = PAGE_PHYS_ADDR(device->pages);
  eval_cmd.trb_type = TRB_EVAL_CTX_CMD;
  eval_cmd.slot_id = device->slot_id;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_run_command(xhci, as_trb(eval_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != CC_SUCCESS) {
    return -EFAILED;
  }

  return 0;
}

// reset endpoint

// reset device

//
// MARK: Events
//

void *xhci_wait_for_transfer(xhci_device_t *device) {
  cond_wait(&device->xhci->event_ack);
  return device->xhci->event_thread->data;
}

bool xhci_has_dequeue_event(xhci_intr_t *intr) {
  xhci_ring_t *ring = intr->ring;
  xhci_trb_t *trb = &ring->ptr[ring->index];
  return trb->trb_type != 0 && trb->cycle == ring->ccs;
}

bool xhci_has_event(xhci_intr_t *intr) {
  xhci_ring_t *ring = intr->ring;
  xhci_trb_t *trb = &ring->ptr[ring->rd_index];
  return trb->trb_type != 0 && trb->cycle == ring->ccs;
}

//
// MARK: Devices
//

xhci_device_t *xhci_alloc_device(xhci_dev_t *xhci, xhci_port_t *port, uint8_t slot) {
  // input context
  page_t *ictx_page = valloc_zero_pages(1, PG_WRITE | PG_NOCACHE);
  xhci_input_ctx_t *ictx = (void *) PAGE_VIRT_ADDR(ictx_page);
  xhci_slot_ctx_t *slot_ctx = &ictx->slot;
  xhci_endpoint_ctx_t *ep_ctx = &ictx->endpoint[0];

  // set A0 and A1 flags to 1
  ictx->ctrl.add_flags |= 0x3;

  slot_ctx->root_hub_port = port->number;
  slot_ctx->route_string = 0;
  slot_ctx->speed = port->speed;
  slot_ctx->ctx_entries = 1;

  // allocate transfer ring
  xhci_ring_t *ring = xhci_alloc_ring();

  // endpoint context
  ep_ctx->ep_type = XHCI_CTRL_BI_EP;
  ep_ctx->max_packt_sz = 512;
  ep_ctx->max_burst_sz = 0;
  ep_ctx->tr_dequeue_ptr = PAGE_PHYS_ADDR(ring->page) | 1;
  ep_ctx->avg_trb_length = 8;
  ep_ctx->interval = 0;
  ep_ctx->max_streams = 0;
  ep_ctx->mult = 0;
  ep_ctx->cerr = 3;

  // device context
  page_t *dctx_page = valloc_zero_pages(1, PG_WRITE | PG_NOCACHE);
  xhci_device_ctx_t *dctx = (void *) PAGE_VIRT_ADDR(dctx_page);
  xhci->dcbaap[slot] = PAGE_PHYS_ADDR(dctx_page);

  ictx_page->next = dctx_page;

  // device struct
  xhci_device_t *device = kmalloc(sizeof(xhci_device_t));
  device->slot_id = slot;
  device->dev_class = 0;
  device->dev_subclass = 0;
  device->dev_protocol = 0;
  device->xhci = xhci;
  device->port = port;
  device->ring = ring;
  device->ictx = ictx;
  device->dctx = dctx;
  device->pages = ictx_page;
  device->endpoints = NULL;
  device->thread = NULL;
  cond_init(&device->event, 0);
  cond_init(&device->event_ack, 0);

  xhci_intr_t *intr = xhci_setup_interrupter(xhci, device_irq_callback, device);
  device->intr = intr;
  slot_ctx->intrptr_target = intr->number;

  port->device = device;
  return device;
}

xhci_ep_t *xhci_alloc_device_ep(xhci_device_t *device, usb_ep_descriptor_t *desc) {
  uint8_t ep_num = desc->ep_addr & 0xF;
  uint8_t ep_dir = (desc->ep_addr >> 7) & 0x1;
  uint8_t ep_type = desc->attributes & 0x3;
  uint8_t index = ep_index(ep_num, ep_dir);

  // endpoint struct
  xhci_ep_t *ep = kmalloc(sizeof(xhci_ep_t));
  ep->number = ep_num;
  ep->index = index;
  ep->type = get_ep_type(ep_type, ep_dir);
  ep->dir = ep_dir;
  ep->ctx = &device->ictx->endpoint[index];
  ep->ring = xhci_alloc_ring();
  cond_init(&ep->event, 0);
  ep->next = NULL;

  memset(&ep->last_event, 0, sizeof(usb_event_t));

  xhci_trace_debug("endpoint %d", ep_num);
  xhci_trace_debug("index %d", index);
  xhci_trace_debug("interval %d", desc->interval);
  xhci_trace_debug("%s", get_speed_str(device->port->speed));

  xhci_endpoint_ctx_t *ctx = ep->ctx;
  ctx->ep_type = get_ep_type(ep_type, ep_dir);
  ctx->interval = desc->interval;
  ctx->max_packt_sz = desc->max_pckt_sz;
  ctx->max_burst_sz = 0;
  ctx->tr_dequeue_ptr = PAGE_PHYS_ADDR(ep->ring->page) | 1;
  ctx->avg_trb_length = 8;
  ctx->max_streams = 0;
  ctx->mult = 0;
  ctx->cerr = 3;

  if (device->endpoints == NULL) {
    device->endpoints = ep;
  } else {
    xhci_ep_t *last = device->endpoints;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = ep;
  }

  return ep;
}

int xhci_get_device_configs(xhci_device_t *device) {
  usb_device_descriptor_t *desc = device->desc;
  usb_config_descriptor_t **configs = kmalloc(sizeof(void *) * desc->num_configs);
  device->configs = configs;
  for (int i = 0; i < desc->num_configs; i++) {
    size_t size;
    usb_config_descriptor_t *config = xhci_get_descriptor(device, CONFIG_DESCRIPTOR, i, &size);
    if (config == NULL) {
      xhci_trace_debug("failed to get config descriptor (%d)", i);
      return -EFAILED;
    }
    configs[i] = config;
  }
  return 0;
}

int xhci_select_device_config(xhci_device_t *device) {
  xhci_dev_t *xhci = device->xhci;
  xhci_input_ctx_t *input = device->ictx;
  usb_device_descriptor_t *desc = device->desc;

  char *device_str = xhci_get_string_descriptor(device, device->desc->product_idx);
  char *manuf_str = xhci_get_string_descriptor(device, device->desc->manuf_idx);
  xhci_log("Device: %s | Manufacturer: %s", device_str, manuf_str);
  kfree(device_str);
  kfree(manuf_str);

  for (int i = 0; i < desc->num_configs; i++) {
    label(try_config);

    usb_config_descriptor_t *config = device->configs[i];
    usb_if_descriptor_t *interfaces = ptr_after(config);

    char *config_str = xhci_get_string_descriptor(device, config->this_idx);
    xhci_log("Config: %s", config_str);
    kfree(config_str);

    for (int j = 0; j < config->num_ifs; j++) {
      usb_if_descriptor_t *interface = &interfaces[j];

      uint8_t class_code = desc->dev_class;
      uint8_t subclass_code = desc->dev_subclass;
      uint8_t protocol = desc->dev_protocol;
      if (class_code == USB_CLASS_NONE) {
        class_code = interface->if_class;
        subclass_code = interface->if_subclass;
        protocol = interface->if_protocol;
      }

      xhci_trace_debug("trying config %d interface %d", i, j);
      xhci_trace_debug("class: %d | subclass: %d | protocol: %d", class_code, subclass_code, protocol);

      for (int k = 0; k < interface->num_eps; k++) {
        usb_ep_descriptor_t *endpoint = usb_get_ep_descriptor(interface, k);

        xhci_ep_t *ep = xhci_alloc_device_ep(device, endpoint);
        input->slot.ctx_entries++;
        input->ctrl.drop_flags = 0;
        input->ctrl.add_flags = 1 | (1 << (ep->index + 1));
        if (xhci_configure_endpoint(xhci, device) != 0) {
          xhci_trace_debug("failed to configure endpoint");
          kfree(ep);
          goto try_config;
        }
      }

      usb_setup_packet_t get_desc = SET_CONFIGURATION(config->config_val);
      xhci_queue_setup(device, &get_desc, SETUP_DATA_NONE);
      xhci_queue_status(device, DATA_OUT);
      xhci_db(device->slot_id)->target = 1;
      cond_wait(&xhci->event_ack);

      xhci_trace_debug("device configured with config %d interface %d", i, j);
      device->dev_class = class_code;
      device->dev_subclass = subclass_code;
      device->dev_protocol = protocol;
      device->dev_config = i;
      device->dev_if = j;
      return 0;
    }
  }

  return -EFAILED;
}

xhci_ep_t *xhci_get_endpoint(xhci_device_t *device, uint8_t ep_num) {
  xhci_ep_t *ep = device->endpoints;
  while (ep != NULL) {
    if (ep->number == ep_num) {
      return ep;
    }
    ep = ep->next;
  }
  return NULL;
}

xhci_ep_t *xhci_find_endpoint(xhci_device_t *device, bool dir) {
  xhci_ep_t *ep = device->endpoints;
  while (ep != NULL) {
    switch (ep->ctx->ep_type) {
      case XHCI_ISOCH_OUT_EP:
      case XHCI_BULK_OUT_EP:
      case XHCI_INTR_OUT_EP:
        if (dir == DATA_OUT) {
          return ep;
        }
        break;
      case XHCI_ISOCH_IN_EP:
      case XHCI_BULK_IN_EP:
      case XHCI_INTR_IN_EP:
        if (dir == DATA_IN) {
          return ep;
        }
        break;
      default:
        break;
    }
    ep = ep->next;
  }
  return NULL;
}

int xhci_ring_device_db(xhci_device_t *device) {
  xhci_dev_t *xhci = device->xhci;
  xhci_db(device->slot_id)->target = 1;
  return 0;
}

//
// MARK: Transfers
//

int xhci_queue_setup(xhci_device_t *device, usb_setup_packet_t *setup, uint8_t type) {
  kassert(xhc != NULL);
  if (type != SETUP_DATA_NONE && type != SETUP_DATA_OUT && type != SETUP_DATA_IN) {
    return EINVAL;
  }

  xhci_setup_trb_t trb;
  clear_trb(&trb);

  memcpy(&trb, setup, sizeof(usb_setup_packet_t));
  trb.trb_type = TRB_SETUP_STAGE;
  trb.trs_length = 8;
  trb.tns_type = type;
  trb.ioc = 1;
  trb.idt = 1;

  xhci_ring_enqueue_trb(device->ring, as_trb(trb));
  return 0;
}

int xhci_queue_data(xhci_device_t *device, uintptr_t buffer, uint16_t size, bool dir) {
  kassert(xhc != NULL);

  xhci_data_trb_t trb;
  clear_trb(&trb);

  trb.trb_type = TRB_DATA_STAGE;
  trb.buf_ptr = _vm_virt_to_phys(buffer);
  trb.trs_length = size;
  trb.td_size = 0;
  trb.dir = dir;

  xhci_ring_enqueue_trb(device->ring, as_trb(trb));
  return 0;
}

int xhci_queue_status(xhci_device_t *device, bool dir) {
  kassert(xhc != NULL);

  xhci_status_trb_t trb;
  clear_trb(&trb);

  trb.trb_type = TRB_STATUS_STAGE;
  trb.dir = dir;
  trb.ioc = 1;

  xhci_ring_enqueue_trb(device->ring, as_trb(trb));
  return 0;
}

int xhci_queue_transfer(xhci_device_t *device, uintptr_t buffer, uint16_t size, bool dir, uint8_t flags) {
  kassert(xhc != NULL);

  xhci_normal_trb_t trb;
  clear_trb(&trb);
  trb.ioc = flags & XHCI_XFER_IOC;
  trb.isp = flags & XHCI_XFER_ISP;
  trb.ns = flags & XHCI_XFER_NS;
  trb.buf_ptr = buffer;
  trb.trs_length = size;
  trb.intr_trgt = device->intr->number;
  trb.trb_type = TRB_NORMAL;

  xhci_ep_t *ep = xhci_find_endpoint(device, dir);
  if (ep == NULL) {
    return -EINVAL;
  }

  xhci_ring_enqueue_trb(ep->ring, as_trb(trb));
  return 0;
}

// descriptors

void *xhci_get_descriptor(xhci_device_t *device, uint8_t type, uint8_t index, size_t *bufsize) {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  xhci_trace_debug("getting descriptor %d", type);

  size_t size = 8;
  label(make_request);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(type, index, size);
  uint8_t *desc = kmalloc(size);
  memset(desc, 0, size);

  xhci_queue_setup(device, &get_desc, SETUP_DATA_IN);
  xhci_queue_data(device, (uintptr_t) desc, size, DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_db(device->slot_id)->target = 1;

  cond_wait(&device->xhci->event_ack);
  void *ptr = device->xhci->event_thread->data;
  xhci_cmd_compl_evt_trb_t *result = ptr;
  if (result->trb_type != TRB_TRANSFER_EVT || result->compl_code != 1) {
    kfree(desc);
    return NULL;
  }

  size_t new_size;
  if (type == CONFIG_DESCRIPTOR) {
    uint16_t *buf = (void *) desc;
    new_size = buf[1];
  } else {
    new_size = desc[0];
  }

  if (new_size > size) {
    size = new_size;
    kfree(desc);
    goto make_request;
  }

  xhci_trace_debug("done!");
  if (bufsize != NULL) {
    *bufsize = size;
  }
  return desc;
}

char *xhci_get_string_descriptor(xhci_device_t *device, uint8_t index) {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  // allocate enough to handle most cases
  size_t size = 64;

 label(make_request);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(STRING_DESCRIPTOR, index, size);
  usb_string_t *string = kmalloc(size);
  xhci_queue_setup(device, &get_desc, SETUP_DATA_IN);
  xhci_queue_data(device, (uintptr_t) string, size, DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_db(device->slot_id)->target = 1;

  cond_wait(&xhci->event_ack);

  if (string->length > size) {
    size = string->length;
    kfree(string);
    goto make_request;
  }

  // number of utf16 characters is string->length - 2
  // and since we're converting to null-terminated ascii
  // we only need half as many characters.
  size_t len = ((string->length - 2) / 2);
  char *ascii = kmalloc(len + 1); // 1 extra null char
  ascii[len] = 0;

  int result = utf16_iconvn_ascii(ascii, string->string, len);
  if (result != len) {
    xhci_trace_debug("string descriptor conversion failed");
  }

  kfree(string);
  return ascii;
}

//
// MARK: TRB Rings
//

xhci_ring_t *xhci_alloc_ring() {
  xhci_ring_t *ring = kmalloc(sizeof(xhci_ring_t));
  ring->page = valloc_zero_pages(1, PG_WRITE);
  ring->ptr = (void *) PAGE_VIRT_ADDR(ring->page);
  ring->index = 0;
  ring->rd_index = 0;
  ring->max_index = PAGE_SIZE / sizeof(xhci_trb_t);
  ring->ccs = 1;
  return ring;
}

void xhci_free_ring(xhci_ring_t *ring) {
  vfree_pages(ring->page);
  kfree(ring);
}

void xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb) {
  trb->cycle = ring->ccs;
  ring->ptr[ring->index++] = *trb;

  if (ring->index == ring->max_index - 1) {
    xhci_link_trb_t link;
    clear_trb(&link);
    link.trb_type = TRB_LINK;
    link.cycle = ring->ccs;
    link.toggle_cycle = 1;
    link.rs_addr = PAGE_PHYS_ADDR(ring->page);
    ring->ptr[ring->index++] = *as_trb(link);

    ring->index = 0;
    ring->ccs = !ring->ccs;
  }
}

void xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t **result) {
  xhci_trb_t *trb = &ring->ptr[ring->index++];

  if (ring->index == ring->max_index) {
    ring->index = 0;
    ring->ccs = !ring->ccs;
  }
  *result = trb;
}

void xhci_ring_read_trb(xhci_ring_t *ring, xhci_trb_t **result) {
  xhci_trb_t *trb = &ring->ptr[ring->rd_index++];

  if (ring->rd_index == ring->max_index) {
    ring->rd_index = 0;
  }
  *result = trb;
}

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

void _xhci_debug_port_registers(xhci_controller_t *hc) {
  // uint32_t
}

void _xhci_debug_device_context(_xhci_device_t *device) {
  xhci_input_ctx_t *ictx = device->ictx;
  xhci_slot_ctx_t *slot_ctx = &ictx->slot;

  kprintf("  address: %d\n", slot_ctx->device_addr);
}
