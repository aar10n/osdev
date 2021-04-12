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

//
static xhci_dev_t *xhc = NULL;

noreturn void *xhci_event_loop(void *arg) {
  xhci_dev_t *xhci = arg;

  kprintf("[xhci] starting event loop\n");

  xhci_op->usbsts.evt_int = 1;
  while (true) {
    if (!xhci_op->usbsts.evt_int) {
      kprintf("[xhci] waiting for event\n");
      cond_wait(&xhci->event);
    }
    kprintf("[xhci] >>>>> an event occurred\n");

    if (xhci_op->usbsts.hc_error) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST CONTROLLER ERROR <<<<<<<");
    } else if (xhci_op->usbsts.hs_err) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST SYSTEM ERROR <<<<<<<\n");
    }

    xhci_ring_t *ring = xhci->intr->ring;
    while (xhci_is_valid_event(xhci->intr)) {
      kprintf("[xhci] getting event\n");
      xhci_trb_t *trb;
      xhci_ring_dequeue_trb(ring, &trb);
      kprintf("[xhci] event type: %d\n", trb->trb_type);
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

  // setup devices
  xhci_port_t *port = xhci->ports;
  while (port) {
    kprintf("[xhci] setting up device on port %d\n", port->number);

    kprintf("[xhci] enabling port %d\n", port->number);
    if (xhci_enable_port(xhci, port) != 0) {
      kprintf("[xhci] failed to enable port %d\n", port->number);
      port = port->next;
      continue;
    }
    kprintf("[xhci] enabled port %d\n", port->number);

    xhci_device_t *device = xhci_setup_device(xhci, port);
    if (device == NULL) {
      kprintf("[xhci] failed to setup device on port %d\n", port->number);
      port = port->next;
      continue;
    }

    kprintf("[xhci] addressing device\n");
    if (xhci_address_device(xhci, device) != 0) {
      kprintf("[xhci] failed to address device\n");
      port = port->next;
      continue;
    }
    kprintf("[xhci] addressed device!\n");

    port = port->next;
  }

  kprintf("[xchi] done initializing!\n");
  thread_join(xhci->event_thread, NULL);
}

//
// public api
//

void xhci_init() {
  kprintf("[xhci] initializing controller\n");

  pcie_device_t *device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (device == NULL) {
    kprintf("[xhci] no xhci controller\n");
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

  kprintf("[xhci] done!\n");
}

void xhci_queue_command(xhci_trb_t *trb) {
  kassert(xhc != NULL);
  xhci_ring_enqueue_trb(xhc->cmd_ring, trb);
}

void *xhci_run_command(xhci_trb_t *trb) {
  kassert(xhc != NULL);
  return xhci_execute_cmd_trb(xhc, trb);
}

