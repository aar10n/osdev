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
  xhci_dev_t *dev = data;
  kprintf(">>>>>> CALLBACK <<<<<<\n");
  cond_signal(&dev->event);
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
  int result = xhci_reset_controller(xhci);
  if (result != 0) {
    return result;
  }

  uint32_t hcsparams1 = read32(CAP, XHCI_CAP_HCSPARAMS1);
  uint8_t max_ports = CAP_MAX_PORTS(hcsparams1);
  uint8_t max_slots = CAP_MAX_SLOTS(hcsparams1);
  kprintf("[xhci] number of ports: %d\n", max_ports);
  kprintf("[xhci] number of slots: %d\n", max_slots);

  // set up the number of enabled slots
  write32(OP, XHCI_OP_CONFIG, CONFIG_MAX_SLOTS_EN(max_slots));

  // set up the device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * max_slots;
  void *dcbaap = kmalloca(dcbaap_size, 64);
  uintptr_t dcbaap_phys = heap_ptr_phys(dcbaap);
  addr_write64(OP, XHCI_OP_DCBAAP, dcbaap_phys);
  xhci->dcbaap = dcbaap;

  // set up the command ring
  xhci_ring_t *ring = xhci_alloc_ring();
  xhci->cmd_ring = ring;
  write64(OP, XHCI_OP_CRCR, ring->page->frame | CRCR_RCS);

  // set up the root interrupter
  xhci_intrptr_t *intrptr = xhci_seutp_interrupter(xhci, 0);
  xhci->intr = intrptr;

  // run the controller
  result = xhci_run_controller(xhci);

  uint32_t usbcmd = read32(OP, XHCI_OP_USBCMD);
  uint32_t usbsts = read32(OP, XHCI_OP_USBSTS);
  uint32_t iman = read32(RT, XHCI_INTR_IMAN(0));

  xhci_noop_cmd_trb_t noop_cmd = {
    .trb_type = TRB_NOOP_CMD
  };

  xhci_cmd_compl_evt_trb_t *event;
  event = xhci_execute_cmd_trb(xhci, as_trb(noop_cmd));
  kprintf("[xhci] noop command | %d\n", event->compl_code);

  return result;
}

int xhci_reset_controller(xhci_dev_t *xhci) {
  kprintf("[xhci] resetting controller\n");

  or_write32(OP, XHCI_OP_USBCMD, USBCMD_HC_RESET);
  WAIT_TIMEOUT(read32(OP, XHCI_OP_USBSTS) & USBSTS_NOT_READY, return ETIMEDOUT);
  return 0;
}

int xhci_run_controller(xhci_dev_t *xhci) {
  kprintf("[xhci] starting controller\n");

  // enable controller interrupts
  or_write32(OP, XHCI_OP_USBCMD, USBCMD_INT_EN | USBCMD_HS_ERR_EN);
  // run the controller
  or_write32(OP, XHCI_OP_USBCMD, USBCMD_RUN);

  WAIT_TIMEOUT(read32(OP, XHCI_OP_USBSTS) & USBSTS_NOT_READY, return ETIMEDOUT);
  return 0;
}

void *xhci_execute_cmd_trb(xhci_dev_t *xhci, xhci_trb_t *trb) {
  xhci_ring_enqueue_trb(xhci->cmd_ring, trb);
  xhci_ring_db(xhci, 0, 0);

  uint32_t usbcmd = read32(OP, XHCI_OP_USBCMD);
  uint32_t usbsts = read32(OP, XHCI_OP_USBSTS);
  uint32_t iman = read32(RT, XHCI_INTR_IMAN(0));

  void *ptr;
  thread_receive(xhci->event_thread, &ptr);
  return ptr;
}

// Ports

int xhci_enable_port(xhci_dev_t *xhci, uint8_t port_num, xhci_protocol_t *proto) {
  if (proto->rev_major == 0x3) {
    // USB 3
    or_write32(OP, XHCI_PORT_SC(port_num), PORTSC_WARM_RESET);
  } else if (proto->rev_major == 0x2) {
    // USB 2
    or_write32(OP, XHCI_PORT_SC(port_num), PORTSC_RESET);
  }

  WAIT_TIMEOUT(!(read32(OP, XHCI_PORT_SC(port_num)) & PORTSC_EN), return ETIMEDOUT);
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
      if (!(read32(OP, XHCI_PORT_SC(i) & PORTSC_CSC))) {
        continue; // no device is connected
      }

      kprintf("[xhci] enabling port %d\n", i);
      if (xhci_enable_port(xhci, i, proto) != 0) {
        kprintf("[xhci] failed to enable port %d\n", i);
        continue;
      }
      kprintf("[xhci] enabled port %d\n", i);

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

  xhci_input_ctx_t *input_ctx = kmalloca(sizeof(xhci_input_ctx_t), 16);
  xhci_slot_ctx_t *slot_ctx = &input_ctx->slot;
  xhci_endpoint_ctx_t *ep_ctx = &input_ctx->endpoint[0];

  memset(input_ctx, 0, sizeof(xhci_input_ctx_t));

  // set A0 and A1 flags to 1
  input_ctx->ctrl.add_flags |= 0x3;

  slot_ctx->root_hub_port = port->number;
  slot_ctx->route_string = 0;
  slot_ctx->ctx_entries = 1;

  // allocate transfer ring
  xhci_ring_t *ring = xhci_alloc_ring();

  // endpoint context
  ep_ctx->ep_type = XHCI_CTRL_BI_EP;
  ep_ctx->max_packt_sz = 10;
  ep_ctx->max_burst_sz = 0;
  ep_ctx->tr_dequeue_ptr = ring->page->frame | 1;
  ep_ctx->interval = 0;
  ep_ctx->max_streams = 0;
  ep_ctx->mult = 0;
  ep_ctx->err_count = 3;

  // device context
  xhci_device_ctx_t *dev_ctx = kmalloca(sizeof(xhci_device_ctx_t), 16);
  memset(dev_ctx, 0, sizeof(xhci_device_ctx_t));
  xhci->dcbaap[slot_id] = heap_ptr_phys(dev_ctx);

  // device struct
  xhci_device_t *device = kmalloc(sizeof(xhci_device_t));
  device->slot_id = slot_id;
  device->port_num = port->number;
  device->ring = ring;
  device->input = input_ctx;
  device->output = dev_ctx;

  port->device = device;
  return device;
}

int xhci_address_device(xhci_dev_t *xhci, xhci_device_t *device) {
  kprintf("[xhci] addressing device\n");

  xhci_addr_dev_cmd_trb_t addr_cmd = { 0 };
  addr_cmd.input_ctx = heap_ptr_phys(device->input);
  addr_cmd.trb_type = TRB_ADDR_DEV_CMD;
  addr_cmd.slot_id = device->slot_id;

  xhci_cmd_compl_evt_trb_t *result;
  result = xhci_execute_cmd_trb(xhci, as_trb(addr_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != 1) {
    kprintf("[xhci] failed to address device\n");
    return EINVAL;
  }

  kprintf("[xhci] success!\n");
  return 0;
}

// Interrupters

xhci_intrptr_t *xhci_seutp_interrupter(xhci_dev_t *xhci, uint8_t n) {
  uint8_t vector = atomic_fetch_add(&intr_vector, 1);
  idt_hook(vector, irq_callback, xhci);
  pcie_enable_msi_vector(xhci->pci_dev, n, vector);

  size_t erst_size = align(sizeof(xhci_erst_entry_t) * 1, 64);
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);

  xhci_ring_t *ring = xhci_alloc_ring();
  erst[0].rs_addr = heap_ptr_phys(ring->page->frame);
  erst[0].rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  xhci_intrptr_t *intrptr = kmalloc(sizeof(xhci_intrptr_t));
  intrptr->vector = vector;
  intrptr->number = n;
  intrptr->ring = ring;
  intrptr->erst = (uintptr_t) erst;

  write32(RT, XHCI_INTR_IMOD(n), IMOD_INTERVAL(4000));
  write32(RT, XHCI_INTR_ERSTSZ(n), ERSTSZ(1));
  addr_write64(RT, XHCI_INTR_ERSTBA(n), heap_ptr_phys(erst));
  addr_write64(RT, XHCI_INTR_ERDP(n), ring->page->frame);
  or_write32(RT, XHCI_INTR_IMAN(n), IMAN_IE);
  return intrptr;
}

xhci_trb_t *xhci_dequeue_event(xhci_dev_t *xhci, xhci_intrptr_t *intrptr) {
  xhci_ring_t *ring = intrptr->ring;
  xhci_trb_t *trb;

  uint8_t n = intrptr->number;
  bool wrapped = xhci_ring_dequeue_trb(ring, &trb);
  or_write32(RT, XHCI_INTR_IMAN(n), 0);

  uintptr_t addr = addr_read64(RT, XHCI_INTR_ERDP(n));
  if (wrapped) {
    addr = ring->page->frame;
  } else {
    addr += sizeof(xhci_trb_t);
  }
  addr_write64(RT, XHCI_INTR_ERDP(n), addr);
  or_write32(RT, XHCI_INTR_ERDP(n), ERDP_BUSY);
  return trb;
}

// Doorbells

void xhci_ring_db(xhci_dev_t *xhci, uint8_t slot, uint16_t endpoint) {
  if ((slot == 0 && endpoint > 0) || (slot > 0 && endpoint != 0)) {
    panic("invalid slot/endpoint combination");
  }

  kprintf("[xhci] ding dong!\n");
  write32(DB, XHCI_DB(slot), DB_TARGET(endpoint));
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
