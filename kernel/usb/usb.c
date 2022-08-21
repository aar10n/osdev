//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#include <usb/usb.h>
#include <usb/xhci.h>
#include <usb/hid.h>
#include <usb/scsi.h>

#include <mm.h>
#include <sched.h>
#include <process.h>
#include <printf.h>
#include <panic.h>

#include <rb_tree.h>
#include <atomic.h>


#define usb_log(str, args...) kprintf("[usb] " str "\n", ##args)

// #define USB_DEBUG
#ifdef USB_DEBUG
#define usb_trace_debug(str, args...) kprintf("[usb] " str "\n", ##args)
#else
#define usb_trace_debug(str, args...)
#endif

#define desc_type(d) (((usb_descriptor_t *)((void *)(d)))->type)
#define desc_length(d) (((usb_descriptor_t *)((void *)(d)))->length)


static rb_tree_t *tree = NULL;
static id_t device_id = 0;
static cond_t init;

// Drivers

usb_driver_t hid_driver = {
  .name = "HID Device Driver",
  .dev_class = USB_CLASS_HID,
  .dev_subclass = 0,
  .init = hid_device_init,
  .handle_event = hid_handle_event,
};

usb_driver_t scsi_driver = {
  .name = "Mass Storage Driver",
  .dev_class = USB_CLASS_STORAGE,
  .dev_subclass = USB_SUBCLASS_SCSI,
  .init = scsi_device_init,
  .handle_event = scsi_handle_event,
};

//

static usb_driver_t *drivers[] = {
  &hid_driver,
  &scsi_driver
};
static size_t num_drivers = sizeof(drivers) / sizeof(usb_driver_t *);

static inline bool is_correct_driver(usb_driver_t *driver, usb_device_t *dev) {
  return driver->dev_class == dev->device->dev_class &&
    (driver->dev_subclass == 0 ? true : driver->dev_subclass == dev->device->dev_subclass);
}

static xhci_ep_t *form_usb_event(usb_device_t *dev, xhci_transfer_evt_trb_t *trb, usb_event_t *event) {
  xhci_device_t *device = dev->device;
  usb_status_t status;
  if (trb->trb_type != TRB_TRANSFER_EVT || trb->compl_code != CC_SUCCESS) {
    status = USB_ERROR;
  } else {
    status = USB_SUCCESS;
  }

  uint8_t ep_num = ep_number(trb->endp_id - 1);
  xhci_ep_t *ep = xhci_get_endpoint(device, ep_num);
  if (ep == NULL) {
    usb_log("error: unable to find corresponding endpoint");
    return NULL;
  }

  event->type = ep->dir == USB_IN ? TRANSFER_IN : TRANSFER_OUT;
  event->device = dev;
  event->status = status;
  event->timestamp = 0;
  return ep;
}

//

noreturn void *usb_event_loop(void *arg) {
  usb_device_t *dev = arg;
  xhci_device_t *device = dev->device;

  thread_yield();

  uint8_t mode = dev->mode;
  uint32_t value = dev->value;
  usb_trace_debug("starting device event loop");
  while (true) {
    // usb_trace_debug("waiting for event");
    if (mode == USB_DEVICE_REGULAR) {
      cond_wait(&device->event_ack);
      usb_trace_debug("event");

      xhci_transfer_evt_trb_t *trb = device->thread->data;
      usb_event_t event;
      xhci_ep_t *ep = form_usb_event(dev, trb, &event);
      if (ep == NULL) {
        continue;
      }

      ep->last_event = event;
      cond_signal(&ep->event);
      if (dev->driver && dev->driver->handle_event) {
        dev->driver->handle_event(&event, dev->driver_data);
      }
    } else if (mode == USB_DEVICE_POLLING) {
      thread_sleep(value);

      while (xhci_has_event(device->intr)) {
        xhci_transfer_evt_trb_t *trb;
        xhci_ring_read_trb(device->intr->ring, (xhci_trb_t **) &trb);

        usb_event_t event;
        xhci_ep_t *ep = form_usb_event(dev, trb, &event);
        if (ep == NULL) {
          continue;
        }

        ep->last_event = event;
        dev->driver->handle_event(&event, dev->driver_data);
        cond_signal(&ep->event);
      }
    }
  }
}

//

void usb_main() {
  usb_log("[#%d] initializing usb", PERCPU_ID);
  tree = create_rb_tree();

  kprintf("usb: haulting\n");
  thread_block();
  unreachable;

  pcie_device_t *xhci_device = pcie_locate_device(
    PCI_SERIAL_BUS_CONTROLLER,
    PCI_USB_CONTROLLER,
    USB_PROG_IF_XHCI
  );
  if (xhci_device == NULL) {
    panic("no xhci controller");
  }

  // xhci_init(xhci_device);
  // WHILE_TRUE;

  xhci_init();
  xhci_setup_devices();
  usb_log("done!");
  cond_signal(&init);
  thread_block();
}

//
// MARK: Core
//

void usb_init() {
  cond_init(&init, 0);
  process_create(usb_main);
  cond_wait(&init);
}

void usb_register_device(xhci_device_t *device) {
  id_t id = atomic_fetch_add(&device_id, 1);
  usb_log("registering usb device %d", id);

  usb_device_t *dev = kmalloc(sizeof(usb_device_t));
  dev->id = id;
  dev->mode = USB_DEVICE_REGULAR;
  dev->value = 0;
  dev->hc = device->xhci;
  dev->device = device;
  dev->driver = NULL;
  dev->driver_data = NULL;
  rb_tree_insert(tree, id, dev);

  usb_log("device class: %d | subclass: %d", device->dev_class, device->dev_subclass);
  usb_log("locating device driver");
  usb_driver_t *driver = NULL;
  for (int i = 0; i < num_drivers; i++) {
    usb_driver_t *d = drivers[i];
    if (is_correct_driver(d, dev)) {
      usb_log("using driver \"%s\"", d->name);
      driver = d;
      break;
    }
  }

  if (driver == NULL) {
    usb_log("no compatible drivers for device %d", id);
    return;
  }

  dev->driver = driver;
  dev->thread = thread_create(usb_event_loop, dev);

  void *ptr = driver->init(dev);
  if (ptr == NULL) {
    usb_log("failed to initialize driver");
    return;
  }
  dev->driver_data = ptr;
  thread_setsched(dev->thread, POLICY_DRIVER, 254);
  thread_yield();
}

usb_device_t *usb_get_device(id_t id) {
  rb_node_t *node = rb_tree_find(tree, id);
  return node->data;
}

//
// MARK: Transfers
//

int usb_start_transfer(usb_device_t *dev, usb_dir_t dir) {
  bool d = dir == USB_IN ? DATA_IN : DATA_OUT;
  xhci_ep_t *ep = xhci_find_endpoint(dev->device, d);
  if (ep == NULL) {
    return -EINVAL;
  }

  uint8_t ep_id = ep->number * 2 + d;
  xhci_ring_db(dev->device->xhci, dev->device->slot_id, ep_id);
  return 0;
}

int usb_add_transfer(usb_device_t *dev, usb_dir_t dir, void *buffer, size_t size) {
  bool d = dir == USB_IN ? DATA_IN : DATA_OUT;
  int result = xhci_queue_transfer(dev->device, (uintptr_t) buffer, size, d, XHCI_XFER_IOC);
  if (result < 0) {
    return -EINVAL;
  }
  return 0;
}

int usb_add_transfer_custom(usb_device_t *dev, usb_dir_t dir, void *buffer, size_t size, uint8_t flags) {
  bool d = dir == USB_IN ? DATA_IN : DATA_OUT;
  int result = xhci_queue_transfer(dev->device, (uintptr_t) buffer, size, d, flags);
  if (result < 0) {
    return -EINVAL;
  }
  return 0;
}

int usb_await_transfer(usb_device_t *dev, usb_dir_t dir) {
  xhci_ep_t *ep = xhci_find_endpoint(dev->device, dir);
  if (ep == NULL) {
    return -EINVAL;
  }

  cond_wait(&ep->event);
  return ep->last_event.status;
}

//
// MARK: Descriptors
//

usb_ep_descriptor_t *usb_get_ep_descriptor(usb_if_descriptor_t *interface, uint8_t index) {
  int i = 0;
  uintptr_t ptr = ((uintptr_t) interface) + interface->length;

  label(find_next);
  while (desc_type(ptr) != EP_DESCRIPTOR) {
    ptr += desc_length(ptr);
  }

  if (i != index) {
    ptr += desc_length(ptr);
    i++;
    goto find_next;
  }

  return (void *) ptr;
}


