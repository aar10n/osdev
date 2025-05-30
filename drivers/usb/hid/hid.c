//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include "hid.h"
#include "hid-report.h"
#include "hid-usage.h"
#include "keyboard.h"
#include "mouse.h"

#include <kernel/alarm.h>
#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/usb/usb.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("hid: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("hid: %s: " x, __func__, ##__VA_ARGS__)

int hid_device_init(usb_device_t *device);
int hid_device_deinit(usb_device_t *device);

usb_driver_t hid_driver = {
  .name = "HID Device Driver",
  .dev_class = USB_CLASS_HID,
  .dev_subclass = 0,

  .init = hid_device_init,
  .deinit = hid_device_deinit,
};

void module_init_register_hid_driver(void *_) {
  if (usb_register_driver(&hid_driver) < 0) {
    EPRINTF("failed to register HID driver\n");
    return;
  }
};
MODULE_INIT(module_init_register_hid_driver);

// HID Buffers

hid_buffer_t *hid_buffer_create(uint16_t alloc_size) {
  hid_buffer_t *buffer = kmalloc(sizeof(hid_buffer_t));
  void *buf = vmalloc(PAGE_SIZE, VM_RDWR);

  buffer->alloc_ptr = virt_to_phys(buf);
  buffer->read_ptr = (uintptr_t) buf;
  buffer->alloc_size = alloc_size;
  buffer->max_index = PAGE_SIZE / alloc_size;
  buffer->virt_base = (uintptr_t) buf;
  return buffer;
}

uintptr_t hid_buffer_alloc(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->alloc_ptr;
  size_t index = (ptr - buffer->phys_base) / (size_t)buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->alloc_ptr = buffer->phys_base;
  } else {
    buffer->alloc_ptr += buffer->alloc_size;
  }
  return ptr;
}

void *hid_buffer_read(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  size_t index = (ptr - buffer->phys_base) / buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->read_ptr = buffer->virt_base;
  } else {
    buffer->read_ptr += buffer->alloc_size;
  }
  return (void *) ptr;
}

void *hid_buffer_read_last(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  if (ptr == buffer->virt_base) {
    return (void *) buffer->virt_base + PAGE_SIZE - buffer->alloc_size;
  }
  return (void *) ptr - buffer->alloc_size;
}

//

int hid_device_event_loop(usb_device_t *usb_dev) {
  DPRINTF("starting device event loop\n");
  hid_device_t *hid_dev = usb_dev->driver_data;
  ASSERT(hid_dev != NULL);

  usb_endpoint_t *endpoint = LIST_FIND(e, &usb_dev->endpoints, list, e->number != 0 && e->dir == USB_IN);
  ASSERT(endpoint != NULL);
  alarm_sleep_ms(100);

  // setup the initial set of transfers
  for (int i = 0; i < 8; i++) {
    usb_add_transfer(usb_dev, USB_IN, hid_buffer_alloc(hid_dev->buffer), hid_dev->size);
  }

  while (true) {
    alarm_sleep_ms(16);
    if (chan_wait(endpoint->event_ch) < 0) {
      DPRINTF("event channel closed\n");
      break;
    }

    // DPRINTF("event\n");

    // handle all events that occurred since we last checked
    usb_event_t event;
    while (chan_recv_noblock(endpoint->event_ch, &event) >= 0) {
      uint8_t *buffer = hid_buffer_read(hid_dev->buffer);
      usb_add_transfer(usb_dev, USB_IN, hid_buffer_alloc(hid_dev->buffer), hid_dev->size);
      hid_dev->handle_input(hid_dev, buffer);
    }
  }

  // TODO: deinitialize device
  todo("deinitialize device");
  DPRINTF("exiting event loop\n");
  return 0;
}

//

void *hid_get_report_descriptor(usb_device_t *device, hid_descriptor_t *hid) {
  usb_setup_packet_t get_report = GET_REPORT_DESCRIPTOR(hid->report_length);
  uint8_t *buffer = kmalloc(hid->report_length);
  memset(buffer, 0, hid->report_length);

  DPRINTF("getting report descriptor\n");
  if (usb_run_ctrl_transfer(device, get_report, kheap_ptr_to_phys(buffer), hid->report_length) < 0) {
    EPRINTF("failed to get report descriptor\n");
    return NULL;
  }

  DPRINTF("report descriptor loaded\n");
  return buffer;
}

int hid_get_idle(usb_device_t *device) {
  usb_setup_packet_t get_idle = GET_IDLE(0, 0);
  uint16_t *idle_rate = kmalloc(sizeof(uint16_t));

  DPRINTF("getting idle rate\n");
  if (usb_run_ctrl_transfer(device, get_idle, kheap_ptr_to_phys(idle_rate), sizeof(uint16_t)) < 0) {
    EPRINTF("failed to get idle rate\n");
    return -1;
  }

  uint16_t idle = *idle_rate;
  DPRINTF("idle loaded\n");
  DPRINTF("idle: %d\n", idle);

  kfree(idle_rate);
  return idle;
}

int hid_set_idle(usb_device_t *device, uint8_t duration) {
  usb_setup_packet_t set_idle = SET_IDLE(duration, 0, 0);

  DPRINTF("setting idle rate to %d\n", duration);
  if (usb_run_ctrl_transfer(device, set_idle, 0, 0) < 0) {
    EPRINTF("failed to set idle rate\n");
    return -1;
  }

  DPRINTF("idle rate set\n");
  return 0;
}

//

int hid_device_init(usb_device_t *device) {
  DPRINTF("initializing device\n");

  usb_if_descriptor_t *interface = device->interface;
  ASSERT(interface != NULL);

  hid_descriptor_t *desc = offset_ptr(interface, interface->length);
  DPRINTF("hid descriptor:\n");
  DPRINTF("  length = %d", desc->length);
  DPRINTF("  type = %d\n", desc->type);
  DPRINTF("  hid_ver = %X\n", desc->hid_ver);
  DPRINTF("  num_descriptors = %d\n", desc->num_descriptors);
  DPRINTF("  class_type = %d\n", desc->class_type);
  DPRINTF("  report_length = %d\n", desc->report_length);

  void *report_desc = hid_get_report_descriptor(device, desc);
  if (report_desc == NULL) {
    EPRINTF("failed to get report descriptor\n");
    return -1;
  }

  report_format_t *format = hid_parse_report_descriptor(report_desc, desc->report_length);
  if (format == NULL) {
    EPRINTF("failed to parse report descriptor\n");
    kfree(report_desc);
    return -1;
  }

  void *fn_ptr = NULL;
  void *data_ptr = NULL;
  collection_node_t *top_level = (void *) format->root->children;
  uint32_t usage_page = top_level->usage_page;
  uint32_t usage = top_level->usage;
  if (usage_page == GENERIC_DESKTOP_PAGE) {
    if (usage == MOUSE_USAGE) {
      DPRINTF("mouse\n");
      fn_ptr = hid_mouse_handle_input;
      data_ptr = hid_mouse_init(format);
      if (data_ptr == NULL) {
        EPRINTF("failed to initialize mouse driver\n");
        return -1;
      }
    } else if (usage == KEYBOARD_USAGE) {
      DPRINTF("keyboard\n");
      fn_ptr = hid_keyboard_handle_input;
      data_ptr = hid_keyboard_init(format);
      if (data_ptr == NULL) {
        EPRINTF("failed to initialize keyboard driver\n");
        return -1;
      }
    } else {
      DPRINTF("hid device not supported: %s\n", hid_get_usage_name(usage_page, usage));
      return -1;
    }
  } else {
    DPRINTF("hid device not supported: %s\n", hid_get_usage_name(usage_page, usage));
    return -1;
  }

  // set_idle sets the reporting rate of the interrupt endpoint
  // we set the device idle to 0 so that the endpoint only reports
  // when the device state changes
  hid_set_idle(device, 0);

  hid_device_t *hid = kmalloc(sizeof(hid_device_t));
  hid->pid = -1;
  hid->desc = desc;
  hid->format = format;
  hid->buffer = hid_buffer_create(format->size);
  hid->size = format->size;
  hid->data = data_ptr;
  hid->handle_input = fn_ptr;

  device->driver_data = hid;

  {
    // create a new process for the hid device
    __ref proc_t *proc = proc_alloc_new(getref(curproc->creds));
    hid->pid = proc->pid;

    // and setup the main thread to handle controller events
    proc_setup_add_thread(proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
    proc_setup_entry(proc, (uintptr_t) hid_device_event_loop, 1, device);
    proc_setup_name(proc, cstr_make("hid_driver"));
    proc_finish_setup_and_submit_all(moveref(proc));
  }

  DPRINTF("done\n");
  return 0;
}

int hid_device_deinit(usb_device_t *device) {
  // TODO: this
  panic("not implemented");
  return -1;
}
