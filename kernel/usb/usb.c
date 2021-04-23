//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#include <usb/usb.h>
#include <usb/xhci.h>
#include <usb/hid.h>
#include <printf.h>
#include <rb_tree.h>
#include <atomic.h>
#include <mm.h>
#include <scheduler.h>

#define usb_log(str, args...) kprintf("[usb] " str "\n", ##args)

#define USB_DEBUG
#ifdef USB_DEBUG
#define usb_trace_debug(str, args...) kprintf("[usb] " str "\n", ##args)
#else
#define usb_trace_debug(str, args...)
#endif

#define desc_type(d) (((usb_descriptor_t *)((void *)(d)))->type)
#define desc_length(d) (((usb_descriptor_t *)((void *)(d)))->length)


static rb_tree_t *tree = NULL;
static id_t device_id = 0;

// Drivers

usb_driver_t hid_driver = {
  .name = "HID Device Driver",
  .dev_class = USB_CLASS_HID,
  .dev_subclass = 0,
  .init = hid_device_init,
  .handle_event = hid_handle_event,
};

//

static usb_driver_t *drivers[] = {
  &hid_driver
};
static size_t num_drivers = sizeof(drivers) / sizeof(usb_driver_t *);

static inline bool is_correct_driver(usb_driver_t *driver, usb_device_t *dev) {
  return driver->dev_class == dev->device->dev_class &&
    (driver->dev_subclass == 0 ? true : driver->dev_subclass == dev->device->dev_subclass);
}

//

noreturn void *usb_event_loop(void *arg) {
  usb_device_t *dev = arg;

  usb_trace_debug("starting device event loop");
  while (true) {
    // usb_trace_debug("waiting for event");
    cond_wait(&dev->device->event_ack);
    // usb_trace_debug("event");

    xhci_transfer_evt_trb_t *trb = dev->device->thread->data;
    usb_status_t status;
    if (trb->trb_type != TRB_TRANSFER_EVT || trb->compl_code != CC_SUCCESS) {
      status = USB_ERROR;
    } else {
      status = USB_SUCCESS;
    }

    usb_event_t event = {
      .type = TRANSFER_IN,
      .device = dev,
      .status = status,
      .timestamp = 0,
    };
    dev->driver->handle_event(&event, dev->driver_data);
  }
}

void usb_main() {
  usb_log("initializing usb");
  tree = create_rb_tree();
  xhci_init();
  xhci_setup_devices();
  usb_log("done!");
  thread_block();
}

//
// MARK: Core
//

void usb_init() {
  process_create(usb_main);
}

void usb_register_device(xhci_device_t *device) {
  id_t id = atomic_fetch_add(&device_id, 1);
  usb_log("registering usb device %d", id);

  usb_device_t *dev = kmalloc(sizeof(usb_device_t));
  dev->id = id;
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

  void *ptr = driver->init(dev);
  if (ptr == NULL) {
    usb_log("failed to initialize driver");
    return;
  }
  dev->driver = driver;
  dev->driver_data = ptr;
  dev->thread = thread_create(usb_event_loop, dev);
  thread_setsched(dev->thread, SCHED_DRIVER, PRIORITY_HIGH);
  thread_yield();
}

//
// MARK: Transfers
//

int usb_add_transfer(usb_device_t *dev, usb_dir_t dir, void *buffer, size_t size) {
  bool tdir = dir == USB_IN ? DATA_IN : DATA_OUT;
  int result = xhci_queue_transfer(dev->device, (uintptr_t) buffer, size, tdir);
  if (result < 0) {
    return -EINVAL;
  }
  return 0;
}

//
// MARK: Descriptors
//

usb_ep_descriptor_t *usb_get_ep_descriptors(usb_if_descriptor_t *interface) {
  uintptr_t ptr = ((uintptr_t) interface) + interface->length;
  while (desc_type(ptr) != EP_DESCRIPTOR) {
    ptr += desc_length(ptr);
  }
  return (void *) ptr;
}


