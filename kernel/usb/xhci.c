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

  while (true) {
    if (!(read32(OP, XHCI_OP_USBSTS) & USBSTS_EVT_INT)) {
      kprintf("[xhci] waiting for event\n");
      cond_wait(&xhci->event);
    }
    kprintf("[xhci] >>>>> an event occurred\n");

    if (read32(OP, XHCI_OP_USBSTS) & USBSTS_HC_ERR) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST CONTROLLER ERROR <<<<<<<");
    } else if (read32(OP, XHCI_OP_USBSTS) & USBSTS_HS_ERR) {
      kprintf("[xhci] an error occurred\n");
      panic(">>>>>>> HOST SYSTEM ERROR <<<<<<<\n");
    }

    xhci_trb_t *trb = xhci_dequeue_event(xhci, xhci->intr);

    // acknowledge event
    or_write32(OP, XHCI_OP_USBSTS, USBSTS_EVT_INT);
    kprintf("[xhci] event acknowledged\n");

    thread_send(trb);
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
    xhci_device_t *device = xhci_setup_device(xhci, port);
    if (device == NULL) {
      kprintf("[xhci] failed to setup device on port %d\n", port->number);
      continue;
    }

    kprintf("[xhci] addressing device\n");
    if (xhci_address_device(xhci, device) != 0) {
      kprintf("[xhci] failed to address device\n");
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
  xhci->op_base = virt_addr + CAP_LENGTH(read32(CAP, XHCI_CAP_LENGTH));
  xhci->rt_base = virt_addr + read32(CAP, XHCI_CAP_RTSOFF);
  xhci->db_base = virt_addr + read32(CAP, XHCI_CAP_DBOFF);

  uint32_t hccparams1 = read32(CAP, XHCI_CAP_HCCPARAMS1);
  xhci->xcap_base = virt_addr + (HCCPARAMS1_XECP(hccparams1) << 2);

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

