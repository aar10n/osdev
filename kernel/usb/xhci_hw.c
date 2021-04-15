//
// Created by Aaron Gill-Braun on 2021-04-04.
//

#include <usb/xhci_hw.h>
#include <usb/xhci.h>
#include <cpu/idt.h>
#include <atomic.h>
#include <printf.h>
#include <panic.h>
#include <thread.h>

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

static uint8_t intr_vector = 0x32;

static void irq_callback(uint8_t vector, void *data) {
  xhci_dev_t *xhci = data;
  if (xhci_op->usbsts.evt_int) {
    kprintf(">>>>>> CALLBACK <<<<<<\n");
    cond_signal(&xhci->event);
  }
}

// Controller

int xhci_init_controller(xhci_dev_t *xhci) {
  // take ownership of xhc if needed
  xhci_cap_t *cap_ptr = NULL;
  cap_ptr = xhci_get_cap(xhci, cap_ptr, XHCI_CAP_LEGACY);
  if (cap_ptr != NULL) {
    kprintf("[xhci] taking ownership\n");
    xhci_cap_legacy_t *legacy = (void *) cap_ptr;
    legacy->os_sem = 1;

    WAIT_TIMEOUT(legacy->os_sem == 1, return ETIMEDOUT);
    kprintf("[xhci] took ownership\n");
  }

  // reset the controller
  kprintf("[xhci] resetting controller\n");

  xhci_op->usbcmd.run = 0;
  xhci_op->usbcmd.hc_reset = 1;
  WAIT_TIMEOUT(xhci_op->usbsts_r & USBSTS_NOT_READY, return ETIMEDOUT);

  uint8_t max_ports = xhci_cap->hcsparams1.max_ports;
  uint8_t max_slots = xhci_cap->hcsparams1.max_slots;
  xhci_op->config.max_slots_en = max_slots;
  kprintf("[xhci] number of ports: %d\n", max_ports);
  kprintf("[xhci] number of slots: %d\n", max_slots);

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
  xhci_intrptr_t *intrptr = xhci_setup_interrupter(xhci, 0);
  xhci->intr = intrptr;

  // run the controller
  kprintf("[xhci] starting controller\n");

  uint32_t usbsts = read32(OP, XHCI_OP_USBSTS);
  uint32_t usbcmd = read32(OP, XHCI_OP_USBCMD);

  xhci_op->usbcmd_r |= USBCMD_HS_ERR_EN | USBCMD_RUN;

  usbsts = read32(OP, XHCI_OP_USBSTS);
  usbcmd = read32(OP, XHCI_OP_USBCMD);

  xhci_op->usbcmd_r |= USBCMD_INT_EN;

  WAIT_TIMEOUT(xhci_op->usbsts_r & USBSTS_NOT_READY, return ETIMEDOUT);

  cond_signal(&xhci->init);
  return 0;
}

void *xhci_execute_cmd_trb(xhci_dev_t *xhci, xhci_trb_t *trb) {
  xhci_ring_enqueue_trb(xhci->cmd_ring, trb);
  xhci_ring_db(xhci, 0, 0);

  void *ptr;
  thread_receive(xhci->event_thread, &ptr);
  return ptr;
}

// Ports

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

xhci_port_t *xhci_discover_ports(xhci_dev_t *xhci) {
  kprintf("[xhci] initializing ports\n");

  xhci_port_t *ports = NULL;
  xhci_port_t *last = NULL;

  xhci_protocol_t *proto = xhci->protocols;
  while (proto) {
    kprintf("[xhci] USB %x.%x supported\n", proto->rev_major, proto->rev_minor);
    for (int i = proto->port_offset; i < proto->port_offset + proto->port_count; i++) {
      if (!(xhci_port(i)->portsc.ccs)) {
        continue; // no device is connected
      }

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

// Interrupters

xhci_intrptr_t *xhci_setup_interrupter(xhci_dev_t *xhci, uint8_t n) {
  uint8_t vector = atomic_fetch_add(&intr_vector, 1);
  idt_hook(vector, irq_callback, xhci);
  pcie_enable_msi_vector(xhci->pci_dev, n, vector);

  size_t erst_size = align(sizeof(xhci_erst_entry_t) * 1, 64);
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  xhci_ring_t *ring = xhci_alloc_ring();
  erst[0].rs_addr = ring->page->frame;
  erst[0].rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  xhci_intrptr_t *intrptr = kmalloc(sizeof(xhci_intrptr_t));
  intrptr->vector = vector;
  intrptr->number = n;
  intrptr->ring = ring;
  intrptr->erst = (uintptr_t) erst;

  xhci_intr(n)->imod.imodi = 4000;
  xhci_intr(n)->erstsz = 1;
  xhci_intr(n)->erstba_r = heap_ptr_phys(erst);
  xhci_intr(n)->erdp_r = ring->page->frame;
  xhci_intr(n)->iman.ie = 1;
  return intrptr;
}

bool xhci_is_valid_event(xhci_intrptr_t *intrptr) {
  xhci_ring_t *ring = intrptr->ring;
  xhci_trb_t *trb = &ring->ptr[ring->index];
  return trb->trb_type != 0 && trb->cycle == ring->ccs;
}

// Doorbells

void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint) {
  // if ((slot == 0 && endpoint > 0) || (slot > 0 && endpoint != 0)) {
  //   panic("invalid slot/endpoint combination");
  // }

  kprintf("[xhci] ding dong!\n");
  xhci_db(slot)->target = endpoint;
}
// Devices

xhci_device_t *xhci_setup_device(xhci_dev_t *xhci, xhci_port_t *port) {
  // allocate slot for port
  xhci_enabl_slot_cmd_trb_t enbl_cmd = { 0 };
  enbl_cmd.trb_type = TRB_ENABL_SLOT_CMD;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_run_command(as_trb(enbl_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != 1) {
    kprintf("[xhci] failed to assign device slot\n");
    return NULL;
  }

  uint8_t slot_id = result->slot_id;

  // input context
  page_t *input_page = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci_input_ctx_t *input_ctx = (void *) input_page->addr;
  xhci_slot_ctx_t *slot_ctx = &input_ctx->slot;
  xhci_endpoint_ctx_t *ep_ctx = &input_ctx->endpoint[0];

  // set A0 and A1 flags to 1
  input_ctx->ctrl.add_flags |= 0x3;

  slot_ctx->root_hub_port = 2;
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
  xhci->dcbaap[slot_id] = output_page->frame;

  // device struct
  xhci_device_t *device = kmalloc(sizeof(xhci_device_t));
  device->slot_id = slot_id;
  device->port_num = port->number;
  device->ring = ring;
  device->input_page = input_page;
  device->input = input_ctx;
  device->output_page = output_page;
  device->output = output_ctx;

  port->device = device;
  return device;
}

int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device) {
  xhci_addr_dev_cmd_trb_t addr_cmd = { 0 };
  addr_cmd.input_ctx = device->input_page->frame;
  addr_cmd.trb_type = TRB_ADDR_DEV_CMD;
  addr_cmd.slot_id = device->slot_id;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_execute_cmd_trb(xhci, as_trb(addr_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != 1) {
    kprintf("[xhci] failed to address device\n");
    return EINVAL;
  }

  return 0;
}

// Capabilities

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
// TRB
//



// Rings

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

bool xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb) {
  trb->cycle = ring->ccs;
  ring->ptr[ring->index++] = *trb;

  bool wrapped = false;
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
    wrapped = true;
  }
  return wrapped;
}

bool xhci_ring_dequeue_trb(xhci_ring_t *ring, xhci_trb_t **result) {
  xhci_trb_t *trb = &ring->ptr[ring->index++];

  bool wrapped = false;
  if (ring->index == ring->max_index) {
    ring->index = 0;
    ring->ccs = !ring->ccs;
    wrapped = true;
  }
  *result = trb;
  return wrapped;
}
