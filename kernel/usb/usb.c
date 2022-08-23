//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#include <usb/usb.h>
#include <usb/xhci.h>
// #include <usb/hid.h>
// #include <usb/scsi.h>

#include <mm.h>
#include <sched.h>
#include <process.h>
#include <printf.h>
#include <panic.h>
#include <string.h>

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

// usb_driver_t hid_driver = {
//   .name = "HID Device Driver",
//   .dev_class = USB_CLASS_HID,
//   .dev_subclass = 0,
//   .init = hid_device_init,
//   .handle_event = hid_handle_event,
// };
//
// usb_driver_t scsi_driver = {
//   .name = "Mass Storage Driver",
//   .dev_class = USB_CLASS_STORAGE,
//   .dev_subclass = USB_SUBCLASS_SCSI,
//   .init = scsi_device_init,
//   .handle_event = scsi_handle_event,
// };
//
// //
//
// static usb_driver_t *drivers[] = {
//   &hid_driver,
//   &scsi_driver
// };
// static size_t num_drivers = sizeof(drivers) / sizeof(usb_driver_t *);


static inline void *usb_seek_descriptor(uint8_t type, void *buffer, void *buffer_end) {
  void *ptr = buffer;
  void *eptr = buffer_end;
  while (ptr < eptr) {
    if (cast_usb_desc(ptr)->type == type) {
      return ptr;
    } else if (cast_usb_desc(ptr)->length == 0) {
      return NULL;
    }
    ptr = offset_ptr(ptr, cast_usb_desc(ptr)->length);
  }
  return NULL;
}

// noreturn void *usb_event_loop(void *arg) {
//   usb_dev_t *dev = arg;
//   xhci_device_t *device = dev->device;
//
//   thread_yield();
//
//   uint8_t mode = dev->mode;
//   uint32_t value = dev->value;
//   usb_trace_debug("starting device event loop");
//   while (true) {
//     // usb_trace_debug("waiting for event");
//     if (mode == USB_DEVICE_REGULAR) {
//       cond_wait(&device->event_ack);
//       usb_trace_debug("event");
//
//       xhci_transfer_evt_trb_t *trb = device->thread->data;
//       usb_event_t event;
//       xhci_ep_t *ep = form_usb_event(dev, trb, &event);
//       if (ep == NULL) {
//         continue;
//       }
//
//       ep->last_event = event;
//       cond_signal(&ep->event);
//       if (dev->driver && dev->driver->handle_event) {
//         dev->driver->handle_event(&event, dev->driver_data);
//       }
//     } else if (mode == USB_DEVICE_POLLING) {
//       thread_sleep(value);
//
//       while (xhci_has_event(device->intr)) {
//         xhci_transfer_evt_trb_t *trb;
//         xhci_ring_read_trb(device->intr->ring, (xhci_trb_t **) &trb);
//
//         usb_event_t event;
//         xhci_ep_t *ep = form_usb_event(dev, trb, &event);
//         if (ep == NULL) {
//           continue;
//         }
//
//         ep->last_event = event;
//         dev->driver->handle_event(&event, dev->driver_data);
//         cond_signal(&ep->event);
//       }
//     }
//   }
// }

//

void usb_main() {
  kprintf("usb: initializing usb\n");

  kprintf("usb: haulting\n");
  thread_block();
  unreachable;
}

void usb_init() {
  cond_init(&init, 0);
  process_create(usb_main);
  cond_wait(&init);
}

//
//
// MARK: New API
//
//

// MARK: Host Driver API

int usb_register_host(usb_host_t *host) {
  kprintf("usb: registering host controller '%s'\n", host->name);

  usb_hub_t *root = kmalloc(sizeof(usb_hub_t));
  root->port = 0;
  root->tier = 0;
  root->data = NULL;
  root->self = NULL;
  root->host = host;
  root->num_devices = 0;
  LIST_INIT(&root->devices);
  host->root = root;

  if (host->host_impl->init(host) < 0) {
    kprintf("usb: failed to initialize host controller\n");
    return -1;
  }

  kprintf("usb: starting host controller\n");
  if (host->host_impl->start(host) < 0) {
    kprintf("usb: failed to start host controller\n");
    return -1;
  }

  kprintf("usb: discovering devices\n");
  if (host->host_impl->discover(host) < 0) {
    kprintf("usb: failed to start host controller\n");
    return -1;
  }
  return 0;
}

int usb_handle_device_connect(usb_host_t *host, void *data) {
  kprintf("usb: >> usb device connected <<\n");

  usb_device_t *device = kmalloc(sizeof(usb_device_t));
  memset(device, 0, sizeof(usb_device_t));
  device->host = host;
  device->data = data;
  LIST_ENTRY_INIT(&device->list);

  if (host->device_impl->init(device) < 0) {
    kprintf("usb: failed to initialize device\n");
  }
  return 0;
}

int usb_handle_device_disconnect(usb_host_t *host, usb_device_t *device) {
  kprintf("usb: >> usb device disconnected <<\n");
  return 0;
}

// MARK: Common API

usb_transfer_t *usb_alloc_transfer(usb_dir_t direction, uintptr_t buffer, size_t length) {
  usb_transfer_t *xfer = kmalloc(sizeof(usb_transfer_t));
  xfer->flags = 0;
  xfer->dir = direction;
  xfer->buffer = buffer;
  xfer->length = length;
  return xfer;
}

usb_transfer_t *usb_alloc_setup_transfer(usb_setup_packet_t setup, uintptr_t buffer, size_t length) {
  usb_transfer_t *xfer = kmalloc(sizeof(usb_transfer_t));
  xfer->flags = USB_XFER_SETUP;
  xfer->setup = setup;
  xfer->buffer = buffer;
  xfer->length = length;
  return xfer;
}

int usb_free_transfer(usb_transfer_t *transfer) {
  kfree(transfer);
  return 0;
}

int usb_device_add_transfer(usb_device_t *device, usb_transfer_t *transfer) {
  // TODO: validation
  usb_host_t *host = device->host;
  return host->device_impl->queue_transfer(device, transfer);
}

int usb_device_await_transfer(usb_device_t *device, usb_transfer_t *transfer) {
  // TODO: validation
  usb_host_t *host = device->host;
  return host->device_impl->await_transfer(device, transfer);
}

//

int usb_device_select_config(usb_device_t *device, uint8_t number) {
  panic("usb: not implemented");
  return 0;
}

int usb_device_select_interface(usb_device_t *device, uint8_t number) {
  panic("usb: not implemented");
  return 0;
}

//

void usb_print_device_descriptor(usb_device_descriptor_t *desc) {
  kprintf("  length = %d | usb_version = %x\n", desc->length, desc->usb_ver);
  kprintf("  class = %d | subclass = %d | protocol = %d\n",
          desc->dev_class, desc->dev_subclass, desc->dev_protocol);
  kprintf("  max_packet_sz_ep0 = %d\n", desc->max_packt_sz0);
  kprintf("  vendor_id = %x | product_id = %x | dev_release = %x\n",
          desc->vendor_id, desc->product_id, desc->dev_release);
  kprintf("  product_idx = %d | manuf_idx = %d | serial_idx = %d\n",
          desc->product_idx, desc->manuf_idx, desc->serial_idx);
  kprintf("  num_configs = %d \n", desc->num_configs);
}

void usb_print_config_descriptor(usb_config_descriptor_t *desc) {
  kprintf("  type = %d | length = %d | total length = %d\n", desc->type, desc->length, desc->total_len);
  kprintf("  num_ifs = %d | config_val = %d | this_idx = %d\n", desc->num_ifs, desc->config_val, desc->this_idx);
  kprintf("  attributes = %#b | max_power = %d\n", desc->attributes, desc->max_power);

  void *ptr = (void *) desc;
  void *ptr_end = offset_ptr(desc, desc->total_len);
  for (int i = 0; i < desc->num_ifs; i++) {
    usb_if_descriptor_t *if_desc = usb_seek_descriptor(IF_DESCRIPTOR, ptr, ptr_end);
    if (if_desc == NULL) {
      return;
    }
    ptr = offset_ptr(if_desc, if_desc->length);

    kprintf("  interface %d\n", if_desc->if_number);
    kprintf("    type = %d | length = %d\n", if_desc->type, if_desc->length);
    kprintf("    num_eps = %d | alt_setting = %d\n", if_desc->num_eps, if_desc->alt_setting);
    kprintf("    if_class = %d | if_subclass = %d | if_protocol = %d\n",
            if_desc->if_class, if_desc->if_subclass, if_desc->if_protocol);
    kprintf("    this_idx = %d\n", if_desc->this_idx);

    for (int j = 0; j < if_desc->num_eps; j++) {
      usb_ep_descriptor_t *ep_desc = usb_seek_descriptor(EP_DESCRIPTOR, ptr, ptr_end);
      if (ep_desc == NULL) {
        return;
      }
      ptr = offset_ptr(ep_desc, ep_desc->length);

      uint8_t ep_num = ep_desc->ep_addr & 0xF;
      uint8_t ep_dir = (ep_desc->ep_addr >> 7) & 1;
      kprintf("    endpoint %d - %s\n", ep_num, ep_dir == USB_EP_IN ? "IN" : "OUT");
      kprintf("      type = %d | length = %d\n", ep_desc->type, ep_desc->length);
      kprintf("      address = %#b | attributes = %#b\n", ep_desc->ep_addr, ep_desc->attributes);
      kprintf("      max_packet_size = %d | interval = %d\n", ep_desc->max_pckt_sz, ep_desc->interval);
    }
  }
}

//
// MARK: Internal API
//


