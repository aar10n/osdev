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

  xhci_setup_devices();

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

void xhci_setup_devices() {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

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
    kprintf("[xhci] device set up\n");

    port->device = device;

    kprintf("[xhci] addressing device\n");
    if (xhci_address_device(xhci, device) != 0) {
      kprintf("[xhci] failed to address device\n");
      port = port->next;
      continue;
    }
    kprintf("[xhci] addressed device!\n");

    // xhci_noop_trb_t noop_trb = {
    //   .trb_type = TRB_NOOP,
    //   .intr_trgt = 0,
    //   .cycle = 1,
    //   .ioc = 1,
    // };
    // xhci_ring_enqueue_trb(device->ring, as_trb(noop_trb));
    // xhci_ring_enqueue_trb(device->ring, as_trb(noop_trb));
    // xhci_db(1)->target = 1


    xhci_get_descriptor(device);

    port = port->next;
  }
}

//

void xhci_queue_command(xhci_trb_t *trb) {
  kassert(xhc != NULL);
  xhci_ring_enqueue_trb(xhc->cmd_ring, trb);
}

void *xhci_run_command(xhci_trb_t *trb) {
  kassert(xhc != NULL);
  return xhci_execute_cmd_trb(xhc, trb);
}

void xhci_run_commands() {
  kassert(xhc != NULL);
  xhci_ring_db(xhc, 0, 0);
  cond_wait(&xhc->event_ack);
}

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

int xhci_queue_data(xhci_device_t *device, void *buffer, uint16_t size, bool dir) {
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

int xhci_run_device(xhci_device_t *device) {
  xhci_ring_db(xhc, device->slot_id, 0);
  return 0;
}

//

void xhci_get_descriptor(xhci_device_t *device) {
  kassert(xhc != NULL);
  xhci_dev_t *xhci = xhc;

  usb_setup_packet_t get_desc = GET_DESCRIPTOR(1, 8);
  void *buffer = kmalloc(8);

  xhci_queue_setup(device, &get_desc, SETUP_DATA_IN);
  xhci_queue_data(device, buffer, 8, DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_db(device->slot_id)->target = 1;

}
