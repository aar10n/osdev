//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#include <usb/xhci.h>
#include <usb/xhci_hw.h>
#include <usb/usb.h>
#include <bus/pcie.h>
#include <mm.h>
#include <printf.h>
#include <panic.h>
#include <string.h>
#include <cpu/idt.h>
#include <scheduler.h>
#include <mutex.h>
#include <atomic.h>
#include <usb/hid.h>

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

//
static xhci_dev_t *xhc = NULL;
static uint8_t intr_vector = 0x32;


static void irq_callback(uint8_t vector, void *data) {
  xhci_dev_t *xhci = data;
  if (xhci_op->usbsts.evt_int) {
    cond_signal(&xhci->event);
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
    while (xhci_is_valid_event(xhci->intr)) {
      xhci_trb_t *trb;
      xhci_ring_dequeue_trb(ring, &trb);
      thread_send(trb);
    }

    uintptr_t addr = ring->page->frame + (ring->index * sizeof(xhci_trb_t));
    xhci_op->usbsts.evt_int = 1;
    xhci_intr(0)->iman.ip = 1;
    xhci_intr(0)->erdp_r = addr | ERDP_BUSY;

    cond_signal(&xhci->event_ack);
  }
}

void xhci_main(xhci_dev_t *xhci) {
  // spawn threads
  xhci->event_thread = thread_create(xhci_event_loop, xhci);
  thread_setsched(xhci->event_thread, SCHED_DRIVER, PRIORITY_HIGH);
  thread_yield();

  // initialize the controller
  if (xhci_init_controller(xhci) != 0) {
    panic("[xhci] failed to initialize controller");
  }

  xhc = xhci;
  cond_signal(&xhci->init);

  // get protocols
  xhci->protocols = xhci_get_protocols(xhci);
  // discover ports
  xhci->ports = xhci_discover_ports(xhci);

  xhci_setup_devices();

  xhci_trace_debug("done initializing!");
  thread_join(xhci->event_thread, NULL);
}

//
// MARK: Core
//

void xhci_init() {
  xhci_trace_debug("initializing controller");

  pcie_device_t *device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (device == NULL) {
    xhci_trace_debug("no xhci controller");
    return;
  }

  xhci_dev_t *xhci = kmalloc(sizeof(xhci_dev_t));
  pcie_bar_t *bar = pcie_get_bar(device, 0);
  uintptr_t phys_addr = bar->phys_addr;
  size_t size = bar->size;

  // map xhci registers
  uintptr_t virt_addr = MMIO_BASE_VA;
  if (!vm_find_free_area(ABOVE, &virt_addr, bar->size)) {
    panic("[xhci] failed to map registers");
  }
  vm_map_vaddr(virt_addr, phys_addr, size, PE_WRITE);
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

  process_create_1(xhci_main, xhci);
  cond_wait(&xhci->init);

  xhci_trace_debug("done!");
}

void xhci_setup_devices() {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  // setup devices
  xhci_port_t *port = xhci->ports;
  bool first = true;
  while (port) {
    // if (first) {
    //   port = port->next;
    //   first = false;
    //   continue;
    // }

    xhci_trace_debug("setting up device on port %d", port->number);

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

    xhci_input_ctx_t *input = device->input;
    input->ctrl.add_flags |= 1;
    input->endpoint[0].max_packt_sz = desc->max_packt_sz0;
    xhci_trace_debug("evaluating context");
    if (xhci_evaluate_context(xhci, device) != 0) {
      xhci_trace_debug("failed to evaluate context");
      port = port->next;
      continue;
    }
    xhci_trace_debug("context evaluated!");

    // input->ctrl.drop_flags = 0;
    // input->ctrl.add_flags = 1 | 4;
    // xhci_configure_endpoint(xhci, device);

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

    if (device->dev_class == USB_CLASS_HID) {
      hid_get_report_descriptor(device);
    }

    port = port->next;
  }
}

//
// MARK: Controller
//

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
  xhci_op->dcbaap = heap_ptr_phys(dcbaap);

  // set up the command ring
  xhci_ring_t *ring = xhci_alloc_ring();
  xhci->cmd_ring = ring;
  xhci_op->crcr_r |= ring->page->frame | CRCR_RCS;

  // set up the root interrupter
  xhci_intr_t *intrptr = xhci_setup_interrupter(xhci, 0);
  xhci->intr = intrptr;

  // run the controller
  xhci_trace_debug("starting controller");
  xhci_op->usbcmd_r |= USBCMD_RUN | USBCMD_INT_EN | USBCMD_HS_ERR_EN;

  WAIT_READY(return -ETIMEDOUT);
  cond_signal(&xhci->init);
  return 0;
}

xhci_intr_t *xhci_setup_interrupter(xhci_dev_t *xhci, uint8_t n) {
  uint8_t vector = atomic_fetch_add(&intr_vector, 1);
  idt_hook(vector, irq_callback, xhci);
  pcie_enable_msi_vector(xhci->pci_dev, n, vector);

  size_t erst_size = align(sizeof(xhci_erst_entry_t) * 1, 64);
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  xhci_ring_t *ring = xhci_alloc_ring();
  erst[0].rs_addr = ring->page->frame;
  erst[0].rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  xhci_intr_t *intr = kmalloc(sizeof(xhci_intr_t));
  intr->vector = vector;
  intr->number = n;
  intr->ring = ring;
  intr->erst = (uintptr_t) erst;

  xhci_intr(n)->imod.imodi = 4000;
  xhci_intr(n)->erstsz = 1;
  xhci_intr(n)->erstba_r = heap_ptr_phys(erst);
  xhci_intr(n)->erdp_r = ring->page->frame;
  xhci_intr(n)->iman.ie = 1;
  return intr;
}

void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint) {
  if ((slot == 0 && endpoint > 0) || (slot > 0 && endpoint != 0)) {
    panic("invalid slot/endpoint combination");
  }

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
      if (!(xhci_port(i)->portsc.ccs)) {
        continue; // no device is connected
      }

      xhci_trace_debug("found device on port %d", i + 1);
      xhci_port_t *p = kmalloc(sizeof(xhci_port_t));
      p->number = i + 1;
      p->protocol = proto;
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
  xhci_ring_enqueue_trb(xhci->cmd_ring, trb);
  xhci_ring_db(xhci, 0, 0);

  void *ptr;
  thread_receive(xhci->event_thread, &ptr);
  return ptr;
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
  addr_cmd.input_ctx = device->input_page->frame;
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
  config_cmd.input_ctx = device->input_page->frame;
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
  eval_cmd.input_ctx = device->input_page->frame;
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
  void *ptr;
  thread_receive(device->xhci->event_thread, &ptr);
  return ptr;
}

bool xhci_is_valid_event(xhci_intr_t *intr) {
  xhci_ring_t *ring = intr->ring;
  xhci_trb_t *trb = &ring->ptr[ring->index];
  return trb->trb_type != 0 && trb->cycle == ring->ccs;
}

//
// MARK: Devices
//

xhci_device_t *xhci_alloc_device(xhci_dev_t *xhci, xhci_port_t *port, uint8_t slot) {
  // input context
  page_t *input_page = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci_input_ctx_t *input_ctx = (void *) input_page->addr;
  xhci_slot_ctx_t *slot_ctx = &input_ctx->slot;
  xhci_endpoint_ctx_t *ep_ctx = &input_ctx->endpoint[0];

  // set A0 and A1 flags to 1
  input_ctx->ctrl.add_flags |= 0x3;

  slot_ctx->root_hub_port = port->number;
  slot_ctx->route_string = 0;
  slot_ctx->speed = 4;
  slot_ctx->ctx_entries = 1;
  slot_ctx->intrptr_target = 0;

  // allocate transfer ring
  xhci_ring_t *ring = xhci_alloc_ring();

  // endpoint context
  ep_ctx->ep_type = XHCI_CTRL_BI_EP;
  ep_ctx->max_packt_sz = 512;
  ep_ctx->max_burst_sz = 0;
  ep_ctx->tr_dequeue_ptr = ring->page->frame | 1;
  ep_ctx->avg_trb_length = 8;
  ep_ctx->interval = 0;
  ep_ctx->max_streams = 0;
  ep_ctx->mult = 0;
  ep_ctx->cerr = 3;

  // output context
  page_t *output_page = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci_device_ctx_t *output_ctx = (void *) output_page->addr;
  xhci->dcbaap[slot] = output_page->frame;

  // device struct
  xhci_device_t *device = kmalloc(sizeof(xhci_device_t));
  device->slot_id = slot;
  device->port_num = port->number;
  device->dev_class = 0;
  device->dev_subclass = 0;
  device->dev_protocol = 0;
  device->xhci = xhci;
  device->ring = ring;
  device->input_page = input_page;
  device->input = input_ctx;
  device->output_page = output_page;
  device->output = output_ctx;

  port->device = device;
  return device;
}

xhci_ep_t *xhci_alloc_device_ep(xhci_device_t *device, uint8_t ep_num) {
  kassert(ep_num > 0 && ep_num < 32);

  // endpoint struct
  xhci_ep_t *ep = kmalloc(sizeof(xhci_ep_t));
  ep->number = ep_num;
  ep->ictx = &device->output->endpoint[ep_num];
  ep->octx = &device->input->endpoint[ep_num];
  ep->ring = xhci_alloc_ring();

  xhci_endpoint_ctx_t *ictx = ep->ictx;
  ictx->ep_type = XHCI_INTR_IN_EP;
  ictx->max_packt_sz = 512;
  ictx->max_burst_sz = 0;
  ictx->tr_dequeue_ptr = ep->ring->page->frame | 1;
  ictx->avg_trb_length = 8;
  ictx->interval = 0;
  ictx->max_streams = 0;
  ictx->mult = 0;
  ictx->cerr = 3;

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
  xhci_input_ctx_t *input = device->input;
  usb_device_descriptor_t *desc = device->desc;

  int ep_num = xhci_get_free_ep(device);
  if (ep_num < 0) {
    xhci_trace_debug("no free endpoints");
    return ep_num;
  }

  for (int i = 0; i < desc->num_configs; i++) {
    usb_config_descriptor_t *config = device->configs[i];
    usb_if_descriptor_t *interfaces = ptr_after(config);

    for (int j = 0; j < config->num_ifs; j++) {
      usb_if_descriptor_t *interface = interfaces + j;

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

      xhci_ep_t *ep = xhci_alloc_device_ep(device, ep_num);
      input->ctrl.drop_flags = 0;
      input->ctrl.add_flags = 1 | (1 << (ep_num + 1));
      if (xhci_configure_endpoint(xhci, device) != 0) {
        xhci_trace_debug("failed to configure endpoint with config %d interface %d", i, j);
        kfree(ep);
        continue;
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
      device->endpoints[ep_num - 1] = ep;
      return 0;
    }
  }

  return -EFAILED;
}

int xhci_get_free_ep(xhci_device_t *device) {
  for (int i = 0; i < sizeof(device->endpoints); i++) {
    if (device->endpoints[i] == NULL) {
      return i + 1;
    }
  }
  return -ENOSPC;
}

int xhci_ring_device_db(xhci_device_t *device) {
  xhci_dev_t *xhci = device->xhci;
  xhci_db(device->slot_id)->target = 1;
  return 0;
}

//
// MARK: Transactions
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
  trb.buf_ptr = heap_ptr_phys(buffer);
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

// descriptors

void *xhci_get_descriptor(xhci_device_t *device, uint8_t type, uint8_t index, size_t *bufsize) {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  xhci_trace_debug("getting descriptor %d", type);

  size_t size = 8;
  label(make_request);
  usb_setup_packet_t get_desc = GET_DESCRIPTOR(type, index, size);
  uint8_t *desc = kmalloc(size);

  xhci_queue_setup(device, &get_desc, SETUP_DATA_IN);
  xhci_queue_data(device, (uintptr_t) desc, size, DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_db(device->slot_id)->target = 1;

  void *ptr;
  thread_receive(xhci->event_thread, &ptr);
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
  ring->page = alloc_zero_page(PE_WRITE);
  ring->ptr = (void *) ring->page->addr;
  ring->index = 0;
  ring->max_index = PAGE_SIZE / sizeof(xhci_trb_t);
  ring->ccs = 1;
  return ring;
}

void xhci_free_ring(xhci_ring_t *ring) {
  vm_unmap_page(ring->page);
  mm_free_page(ring->page);
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
    link.rs_addr = ring->page->frame;
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
