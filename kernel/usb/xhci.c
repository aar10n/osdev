//
// Created by Aaron Gill-Braun on 2021-03-04.
//

#include <usb/xhci.h>
#include <bus/pcie.h>
#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <printf.h>
#include <panic.h>
#include <string.h>
#include <cpu/idt.h>
#include <scheduler.h>
#include <device/pit.h>

static xhci_dev_t *xhci;

// static void *dcbaap;
// static uintptr_t cap_base;
// static uintptr_t op_base;
// static uintptr_t rt_base;

// xhci_cap_regs_t *cap;
// xhci_op_regs_t *op;
// xhci_port_regs_t *ports;
// xhci_rt_regs_t *rt;
// xhci_db_reg_t  *db;
// xhci_trb_t *cmd_ring;
// xhci_trb_t *evt_ring;

//

void callback(uint8_t vector, void *data) {
  xhci_dev_t *dev = data;
  kprintf(">>>>>> CALLBACK <<<<<<\n");
  dev->op->usbsts.evt_int = 1;
}

//

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

//

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

void init_ports(xhci_dev_t *dev) {
  xhci_protocol_t *proto = dev->protocols;
  while (proto) {
    kprintf("[xhci] USB %x.%x supported\n", proto->rev_major, proto->rev_minor);
    for (int i = proto->port_offset; i < proto->port_offset + proto->port_count; i++) {
      xhci_port_regs_t *port = &dev->ports[i];
      if (port->portsc.ccs != 1) {
        // no device is connected
        continue;
      }

      if (proto->rev_major == 0x3) {
        // USB 3
        port->portsc.warm_rst = 1;
      } else if (proto->rev_major == 0x2) {
        // USB 2
        port->portsc.reset = 1;
      }

      if (!port->portsc.enabled) {
        pit_mdelay(5);
        if (!port->portsc.enabled) {
          kprintf("[xhci] failed to enable port %d\n", i);
          continue;
        }
      }

      kprintf("[xhci] enabled port %d\n", i);

      // reset the change flags
      if (proto->rev_major == 0x3) {
        port->portsc.wrc = 1;
      } else {
        port->portsc.prc = 1;
      }
      port->portsc.csc = 1;
    }

    xhci_enabl_slot_cmd_trb_t cmd = {
      .trb_type = TRB_ENABL_SLOT_CMD,
      .cycle = 1,
      .slot_type = 0
    };
    dev->cmd_ring[0] = *((xhci_trb_t *) &cmd);
    dev->db[0].target = 0;

    pit_mdelay(1);

    xhci_port_status_evt_trb_t *event = (void *) &dev->evt_ring[0];
    proto = proto->next;
  }
}



//

void xhci_init() {
  cli();

  pcie_device_t *device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (device == NULL) {
    kprintf("[xhci] no xhci controller\n");
    return;
  }

  xhci = kmalloc(sizeof(xhci_dev_t));
  xhci->pci_dev = device;

  pcie_bar_t *bar = pcie_get_bar(device, 0);
  uintptr_t phys_addr = bar->phys_addr;
  size_t size = bar->size;

  uintptr_t virt_addr = MMIO_BASE_VA;
  if (!vm_find_free_area(ABOVE, &virt_addr, bar->size)) {
    panic("[xhci] failed to map registers");
  }

  vm_map_vaddr(virt_addr, phys_addr, size, PE_WRITE);
  bar->virt_addr = virt_addr;

  xhci_cap_regs_t *cap = (void *) virt_addr;
  xhci_op_regs_t *op = (void *) virt_addr + cap->length;
  xhci_rt_regs_t *rt = (void *) virt_addr + cap->rt_offset;
  xhci_db_reg_t *db = (void *) virt_addr + cap->db_offset;
  xhci_port_regs_t *ports = (void *) virt_addr + cap->length + 0x400;

  xhci->phys_addr = phys_addr;
  xhci->virt_addr = virt_addr;
  xhci->size = bar->size;

  xhci->cap = cap;
  xhci->op = op;
  xhci->rt = rt;
  xhci->db = db;
  xhci->ports = ports;

  kprintf("[xhci] initializing controller\n");

  // reset the controller
  op->usbcmd.hc_reset = 1;

  int attempts = 0;
  while (op->usbsts.not_ready) {
    if (attempts > 5) {
      panic("[xhci] failed to initialize controller");
    }
    pit_mdelay(5);
    attempts++;
  }

  kprintf("[xhci] number of ports: %d\n", cap->hcsparams1.max_ports);
  kprintf("[xhci] number of slots: %d\n", cap->hcsparams1.max_slots);
  kprintf("[xhci] page size: %d\n", op->pagesz);

  // interrupt setup
  idt_hook(0x32, callback, xhci);
  pcie_enable_msi_vector(device, 0, 0x32);

  // device context base array
  size_t dcbaap_size = sizeof(uintptr_t) * cap->hcsparams1.max_slots;
  void *dcbaap = kmalloca(dcbaap_size, 64);

  xhci->dcbaap = dcbaap;
  op->dcbaap = heap_ptr_phys(dcbaap);

  // command ring
  page_t *cr = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci->cmd_ring = (void *) cr->addr;
  op->cmdrctrl.raw = cr->frame | 1;
  op->dnctrl = 2;

  xhci_intr_regs_t *intr = &rt->irs[0];
  intr->ie = 1;
  intr->imodi = 4000;

  // event ring segment table
  size_t erst_size = align(sizeof(xhci_erst_entry_t) * 1, 64);
  xhci_erst_entry_t *erst = kmalloca(erst_size, 64);
  xhci->erst = erst;

  // event ring
  xhci_erst_entry_t *entry = &erst[0];
  page_t *er = alloc_zero_page(PE_WRITE | PE_CACHE_DISABLE);
  xhci->evt_ring = (void *) er->addr;

  entry->rs_addr = er->frame;
  entry->rs_size = PAGE_SIZE / sizeof(xhci_trb_t);

  intr->erstsz = 1;
  intr->erstba = heap_ptr_phys(erst);
  intr->erdp = er->frame;
  intr->ie = 1;

  op->config.max_slots_en = cap->hcsparams1.max_slots;

  op->usbcmd.int_en = 1;
  op->usbcmd.hs_err_en = 1;
  op->usbcmd.run = 1;

  kprintf("[xhci] running\n");

  xhci_noop_cmd_trb_t testcmd;
  memset(&testcmd, 0, sizeof(xhci_trb_t));
  testcmd.cycle = 1;
  testcmd.trb_type = TRB_NOOP_CMD;

  // cmd_ring[0] = *((xhci_trb_t *) &testcmd);
  // db[0].target = 0;

  kprintf("[xhci] sleeping for 2 seconds\n");
  sched_sleep(2e+9);

  sti();

  xhci->protocols = get_protocols(xhci);
  init_ports(xhci);
}
