//
// Created by Aaron Gill-Braun on 2021-02-19.
//

#include <usb/ehci.h>
#include <bus/pci.h>
#include <printf.h>
#include <mm/heap.h>
#include <mm/vm.h>
#include <panic.h>
#include <device/apic.h>
#include <string.h>

// support only one ehci controller for now
static ehci_device_t *ehci_device;
static ehci_cap_regs_t *cap;
static ehci_op_regs_t *op;
static page_t *data_page;
static void *data_ptr;

void ehci_init() {
  pci_device_t *device = pci_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_EHCI
  );
  if (device == NULL) {
    kprintf("[ehci] no ehci controller\n");
    return;
  }

  // // uintptr_t phys_addr = device->bars[0].mem.addr_start;
  // // size_t length = device->bars[0].mem.addr_end - phys_addr;
  // // uintptr_t virt_addr = MMIO_BASE_VA;
  // // if (!vm_find_free_area(ABOVE, &virt_addr, length)) {
  // //   panic("[ehci] no free address space");
  // // }
  //
  // ehci_device_t *ehci = kmalloc(sizeof(ehci_device_t));
  // ehci->phys_addr = phys_addr;
  // ehci->virt_addr = virt_addr;
  // ehci->length = length;
  //
  // vm_map_vaddr(virt_addr, phys_addr, length, PE_WRITE);
  //
  // ehci_device = ehci;
  // cap = (void *) virt_addr;
  // op = (void *)(virt_addr + cap->length);

}

void ehci_host_init() {
  kprintf("[ehci] initializing host controller\n");

  // reset host controller
  op->usbcmd.hc_reset = 1;
  op->usbcmd.raw |= (1 << 1);

  int attempts = 0;
  while (op->usbcmd.hc_reset == 1) {
    if (attempts > 5) {
      panic("failed to reset ehci controller");
    }

    apic_mdelay(1);
    attempts++;
  }

  op->dsegment = 0;
  op->usbintr.usb_en = 1;
  op->usbsts.usb_int = 0;
  op->usbsts.async_sched_sts = 0;
  op->configflag = 1;

  page_t *page = mm_alloc_pages(ZONE_LOW, 1, PE_WRITE | PE_FORCE);
  data_page = page;
  data_ptr = vm_map_page(page);

  memset(data_ptr, 0, PAGE_SIZE);
  ((ehci_qh_t *) data_ptr)->t = 1;
  op->asynclistaddr = (uint32_t) data_page->frame;
  op->usbcmd.async_sched_en = 1;

  kprintf("[ehci] number of ports: %d\n", cap->hcs_params.n_ports);

  kprintf("[ehci] done!\n");

}
