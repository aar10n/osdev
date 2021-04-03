//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#include <usb/xhci.h>
#include <bus/pcie.h>
#include <mm.h>
#include <printf.h>
#include <panic.h>
#include <string.h>
#include <cpu/idt.h>
#include <scheduler.h>
#include <mutex.h>

#define OP_BASE ((uintptr_t)((xhci)->op))
#define get_portsc(n) ((xhci_portsc_t *)(OP_BASE + (0x400 + (0x10 * (n)))))

#define WAIT_TIMEOUT(value, fail)\
{                                \
  int attempts = 0;              \
  while (!(value)) {             \
    if (attempts >= 5) {         \
      fail;                      \
    }                            \
    thread_sleep(1000);          \
    attempts++;                  \
  }                              \
}

static xhci_dev_t *xhci;

//

void xhci_callback(uint8_t vector, void *data) {
  xhci_dev_t *dev = data;
  kprintf(">>>>>> CALLBACK <<<<<<\n");
  cond_signal(&dev->event);
}

//

// xhci_

xhci_cap_t *xhci_get_cap(xhci_dev_t *dev, xhci_cap_t *cap_ptr, uint8_t cap_id) {
  if (cap_ptr == NULL) {
    cap_ptr = (void *) dev->virt_addr + (dev->cap->hccparams1.ext_cap_ptr << 2);
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

xhci_protocol_t *get_protocols(xhci_dev_t *dev) {
  xhci_protocol_t *first = NULL;
  xhci_protocol_t *protos = NULL;

  xhci_cap_t *cap_ptr = NULL;
  while (true) {
    cap_ptr = xhci_get_cap(dev, cap_ptr, XHCI_CAP_PROTOCOL);
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

void xhci_init_device(uint32_t port_num, xhci_protocol_t *proto) {
  // assign a slot for the device
  xhci_enabl_slot_cmd_trb_t enbl_cmd = { 0 };
  enbl_cmd.trb_type = TRB_ENABL_SLOT_CMD;

  xhci_cmd_compl_evt_trb_t *result = xhci_run_command(as_trb(enbl_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != 1) {
    panic("[xhci] failed to assign device slot\n");
  }

  uint8_t slot_id = result->slot_id;

  xhci_input_ctx_t *input_ctx = kmalloca(sizeof(xhci_input_ctx_t), 16);
  xhci_slot_ctx_t *slot_ctx = &input_ctx->slot;
  xhci_endpoint_ctx_t *ep_ctx = &input_ctx->endpoint[0];

  memset(input_ctx, 0, sizeof(xhci_input_ctx_t));
  // set A0 and A1 flags to 1
  input_ctx->ctrl.add_flags |= 0x3;

  slot_ctx->root_hub_port = 1;
  slot_ctx->route_string = 0;
  slot_ctx->ctx_entries = 1;

  // allocate transfer ring
  page_t *ring_page = alloc_zero_page(PE_WRITE);
  xhci_trb_t *ring = (void *) ring_page->addr;

  ep_ctx->ep_type = XHCI_CTRL_BI_EP;
  ep_ctx->max_packt_sz = 10;
  ep_ctx->max_burst_sz = 0;
  ep_ctx->tr_dequeue_ptr = ring_page->frame;
  ep_ctx->tr_dequeue_ptr |= 1; // DCS bit
  ep_ctx->interval = 0;
  ep_ctx->max_streams = 0;
  ep_ctx->mult = 0;
  ep_ctx->err_count = 3;

  xhci_device_ctx_t *dev_ctx = kmalloca(sizeof(xhci_device_ctx_t), 16);
  memset(dev_ctx, 0, sizeof(xhci_device_ctx_t));

  xhci->dcbaap[slot_id] = heap_ptr_phys(dev_ctx);

  xhci_addr_dev_cmd_trb_t addr_cmd = { 0 };
  addr_cmd.input_ctx = heap_ptr_phys(input_ctx);
  addr_cmd.trb_type = TRB_ADDR_DEV_CMD;
  addr_cmd.cycle = 1;
  addr_cmd.slot_id = slot_id;

  result = xhci_run_command(as_trb(addr_cmd));
  kassert(result->trb_type == TRB_CMD_CMPL_EVT);
  if (result->compl_code != 1) {
    panic("[xhci] failed to address device\n");
  }

  kprintf("[xhci] result: %d\n", result->trb_type);
  kprintf("[xhci] success\n");
}

void xhci_init_ports() {
  kprintf("[xhci] initializing ports\n");
  xhci_protocol_t *proto = xhci->protocols;
  while (proto) {
    kprintf("[xhci] USB %x.%x supported\n", proto->rev_major, proto->rev_minor);
    for (int i = proto->port_offset; i < proto->port_offset + proto->port_count; i++) {
      xhci_portsc_t *port = get_portsc(i);
      if (port->ccs != 1) {
        // no device is connected
        continue;
      }

      kprintf("[xhci] resetting port %d\n", i);
      if (proto->rev_major == 0x3) {
        // USB 3
        port->warm_rst = 1;
      } else if (proto->rev_major == 0x2) {
        // USB 2
        port->reset = 1;
      }

      WAIT_TIMEOUT(port->enabled,
        kprintf("[xhci] failed to enable port %d\n", i);
        continue
      );

      kprintf("[xhci] enabled port %d\n", i);

      // reset the change flags
      if (proto->rev_major == 0x3) {
        port->wrc = 1;
      } else {
        port->prc = 1;
      }
      port->csc = 1;

      // initialize the device
      xhci_init_device(i, proto);
      break;
    }

    proto = proto->next;
  }
}

noreturn void *xhci_event_loop(void *arg) {
  kprintf("[xhci] starting event loop\n");

  xhci_cap_regs_t *cap = xhci->cap;
  xhci_op_regs_t *op = xhci->op;
  xhci_rt_regs_t *rt = xhci->rt;
  xhci_db_reg_t *db = xhci->db;
  xhci_port_regs_t *ports = xhci->ports;

  op->usbsts.evt_int = 1;
  while (true) {
    if (!op->usbsts.evt_int) {
      kprintf("[xhci] waiting for event\n");
      cond_wait(&xhci->event);
    }
    kprintf("[xhci] >>>>> an event occurred\n");

    if (op->usbsts.hc_error) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST CONTROLLER ERROR <<<<<<<");
    } else if (op->usbsts.hs_err) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST SYSTEM ERROR <<<<<<<\n");
    }

    xhci_trb_t *trb = &xhci->evt_ring[xhci->evt_index++];

    // acknowledge event
    op->usbsts.evt_int = 1;
    xhci_intr_regs_t *intr = &rt->irs[0];
    uint32_t iman = intr->iman.raw;
    if (iman == 0x2) {
      intr->iman.raw = iman;
    }
    intr->erdp += sizeof(xhci_trb_t);

    kprintf("[xhci] event acknowledged\n");
    thread_send(trb);
    cond_signal(&xhci->event_ack);
  }
}

//
// XHCI API
//

void xhci_init() {
  kprintf("[xhci] pid: %d\n", getpid());

  pcie_device_t *device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (device == NULL) {
    kprintf("[xhci] no xhci controller\n");
    xhci = NULL;
    return;
  }

  xhci = kmalloc(sizeof(xhci_dev_t));
  xhci->pci_dev = device;
  cond_init(&xhci->event);
  cond_init(&xhci->event_ack);

  pcie_bar_t *bar = pcie_get_bar(device, 0);
  uintptr_t phys_addr = bar->phys_addr;
  size_t size = bar->size;

  uintptr_t virt_addr = MMIO_BASE_VA;
  if (!vm_find_free_area(ABOVE, &virt_addr, bar->size)) {
    panic("[xhci] failed to map registers");
  }
  vm_map_vaddr(virt_addr, phys_addr, size, PE_WRITE);
  bar->virt_addr = virt_addr;

  xhci->phys_addr = phys_addr;
  xhci->virt_addr = virt_addr;
  xhci->size = bar->size;

  xhci->cap = (void *) virt_addr;;
  xhci->op = (void *) virt_addr + xhci->cap->length;
  xhci->rt = (void *) virt_addr + xhci->cap->rt_offset;
  xhci->db = (void *) virt_addr + xhci->cap->db_offset;
  xhci->ports = (void *) virt_addr + xhci->cap->length + 0x400;

  // spawn threads
  xhci->event_thread = thread_create(xhci_event_loop, NULL);
  thread_setsched(xhci->event_thread, SCHED_DRIVER, PRIORITY_HIGH);
  thread_yield();

  // interrupt setup
  idt_hook(0x32, xhci_callback, xhci);
  pcie_enable_msi_vector(xhci->pci_dev, 0, 0x32);

  // take ownership (if needed)
  xhci_cap_t *cap_ptr = NULL;
  cap_ptr = xhci_get_cap(xhci, cap_ptr, XHCI_CAP_LEGACY);
  if (cap_ptr != NULL) {
    kprintf("[xhci] taking ownership\n");
    xhci_cap_legacy_t *legacy = (void *) cap_ptr;
    legacy->os_sem = 1;
    while (legacy->os_sem == 1) {
      cpu_pause();
    }
    kprintf("[xhci] took ownership\n");
  }

  // initialize controller
  kprintf("[xhci] initializing controller\n");

  // reset the controller
  xhci->op->usbcmd.hc_reset = 1;

  int attempts = 0;
  while (xhci->op->usbsts.not_ready) {
    if (attempts > 5) {
      panic("[xhci] failed to initialize controller");
    }
    thread_sleep(1000); // 1 ms
    attempts++;
  }

  kprintf("[xhci] number of ports: %d\n", xhci->cap->hcsparams1.max_ports);
  kprintf("[xhci] number of slots: %d\n", xhci->cap->hcsparams1.max_slots);
  kprintf("[xhci] page size: %d\n", xhci->op->pagesz);

  // device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * xhci->cap->hcsparams1.max_slots;
  void *dcbaap = kmalloca(dcbaap_size, 64);
  xhci->dcbaap = dcbaap;
  xhci->op->dcbaap = heap_ptr_phys(dcbaap);

  // command ring
  page_t *cr = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci->cmd_ring = (void *) cr->addr;
  xhci->cmd_ring_phys = cr->frame;
  xhci->cmd_index = 0;
  xhci->cmd_max = PAGE_SIZE / sizeof(xhci_trb_t);
  xhci->cmd_ccs = 1;

  xhci->op->cmdrctrl.raw = cr->frame | 1;
  // xhci->op->dnctrl = 0; // usb_xhci_unimplemented oper write (invalid)

  // event ring segment table
  size_t erst_size = align(sizeof(xhci_erst_entry_t) * 1, 64);
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);
  xhci->erst = erst;

  // event ring
  page_t *er = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci->evt_ring = (void *) er->addr;
  xhci->evt_ring_phys = er->frame;
  xhci->evt_index = 0;
  xhci->evt_max = PAGE_SIZE / sizeof(xhci_trb_t);
  xhci->evt_ccs = 0;

  xhci_erst_entry_t *entry = &erst[0];
  entry->rs_addr = er->frame;
  entry->rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  xhci->op->config.max_slots_en = xhci->cap->hcsparams1.max_slots;
  xhci->op->usbcmd.int_en = 1;
  xhci->op->usbcmd.hs_err_en = 1;

  xhci_intr_regs_t *intr = &xhci->rt->irs[0];
  intr->imodi = 4000;
  intr->erstsz = 1;
  intr->erstba = heap_ptr_phys(erst);
  intr->erdp = er->frame;
  intr->iman.ie = 1;

  xhci->op->usbcmd.run = 1;

  xhci->protocols = get_protocols(xhci);
  xhci_init_ports();

  kprintf("[xchi] done initializing!\n");
  thread_join(xhci->event_thread, NULL);
}

void xhci_queue_command(xhci_trb_t *trb) {
  if (xhci == NULL) {
    return;
  }

  kprintf("[xhci] queuing command trb\n");

  uint32_t i = xhci->cmd_index;
  uint8_t ccs = xhci->cmd_ccs;

  trb->cycle = ccs;
  memcpy(&xhci->cmd_ring[i], trb, sizeof(xhci_trb_t));
  i++;

  if (i == xhci->cmd_max - 1) {
    xhci_link_trb_t link;
    memset(&link, 0, sizeof(xhci_link_trb_t));

    link.trb_type = TRB_LINK;
    link.cycle = ccs;
    link.toggle_cycle = 1;
    link.rs_addr = xhci->evt_ring_phys;

    memcpy(&xhci->cmd_ring[i], &link, sizeof(xhci_trb_t));

    i = 0;
    ccs = !ccs;
  }

  xhci->cmd_index = i;
  xhci->cmd_ccs = ccs;

  kprintf("[xhci] command queued\n");
  // kprintf("[xhci] sending command\n");
  // cond_wait(&xhci->event_ack);
  // kprintf("[xhci] command complete\n");
}

void xhci_ring(uint8_t slot, uint16_t endpoint) {
  if ((slot == 0 && endpoint > 0) || (slot > 0 && endpoint == 0)) {
    panic("invalid slot/endpoint combination");
  }

  kprintf("[xhci] ding dong!\n");
  xhci_db_reg_t *db = &xhci->db[slot];
  db->target = endpoint;
}

void *xhci_run_command(xhci_trb_t *trb) {
  xhci_queue_command(trb);
  xhci_ring(0, 0);

  void *ptr;
  thread_receive(xhci->event_thread, &ptr);
  return ptr;
}
