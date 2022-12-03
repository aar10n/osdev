//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/hid-usage.h>

#include <usb/keyboard.h>
#include <usb/mouse.h>
#include <usb/usb.h>

#include <mm.h>
#include <event.h>
#include <printf.h>
#include <panic.h>
#include <string.h>

#define hid_log(str, args...) kprintf("[hid] " str "\n", ##args)

// #define HID_DEBUG
#ifdef HID_DEBUG
#define hid_trace_debug(str, args...) kprintf("[hid] " str "\n", ##args)
#else
#define hid_trace_debug(str, args...)
#endif

// HID Buffers

hid_buffer_t *hid_buffer_create(uint16_t alloc_size) {
  hid_buffer_t *buffer = kmalloc(sizeof(hid_buffer_t));
  page_t *page = valloc_zero_pages(1, PG_WRITE);

  buffer->alloc_ptr = PAGE_PHYS_ADDR(page);
  buffer->read_ptr = PAGE_VIRT_ADDR(page);
  buffer->alloc_size = alloc_size;
  buffer->max_index = PAGE_SIZE / alloc_size;
  buffer->page = page;
  return buffer;
}

uintptr_t hid_buffer_alloc(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->alloc_ptr;
  int index = (ptr - PAGE_PHYS_ADDR(buffer->page)) / buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->alloc_ptr = PAGE_PHYS_ADDR(buffer->page);
  } else {
    buffer->alloc_ptr += buffer->alloc_size;
  }
  return ptr;
}

void *hid_buffer_read(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  int index = (ptr - PAGE_VIRT_ADDR(buffer->page)) / buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->read_ptr = PAGE_VIRT_ADDR(buffer->page);
  } else {
    buffer->read_ptr += buffer->alloc_size;
  }
  return (void *) ptr;
}

void *hid_buffer_read_last(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  if (ptr == PAGE_VIRT_ADDR(buffer->page)) {
    return (void *) PAGE_VIRT_ADDR(buffer->page) + PAGE_SIZE - buffer->alloc_size;
  }
  return (void *) ptr - buffer->alloc_size;
}

//

noreturn void *hid_device_event_loop(void *arg) {
  usb_device_t *device = arg;
  hid_device_t *hid_device = device->driver_data;
  kassert(hid_device != NULL);

  usb_endpoint_t *endpoint = LIST_FIND(e, &device->endpoints, list, e->number != 0 && e->dir == USB_IN);
  kassert(endpoint != NULL);
  // thread_sleep(MS_TO_US(1000));

  for (int i = 0; i < 8; i++) {
    usb_add_transfer(device, USB_IN, hid_buffer_alloc(hid_device->buffer), hid_device->size);
  }

  kprintf("hid: starting device event loop\n");
  while (true) {
    thread_sleep(MS_TO_US(16));
    // kprintf("hid: checking\n");
    // chan_wait(endpoint->event_ch);

    hid_trace_debug("event");

    // handle all events that occurred since we last checked
    usb_event_t event;
    while (chan_recv_noblock(endpoint->event_ch, chan_voidp(&event)) >= 0) {
      uint8_t *buffer = hid_buffer_read(hid_device->buffer);
      usb_add_transfer(device, USB_IN, hid_buffer_alloc(hid_device->buffer), hid_device->size);
      hid_device->handle_input(hid_device, buffer);
    }
  }

  unreachable;
}

//

void *hid_get_report_descriptor(usb_device_t *device, hid_descriptor_t *hid) {
  usb_setup_packet_t get_report = GET_REPORT_DESCRIPTOR(hid->report_length);
  uint8_t *buffer = kmalloc(hid->report_length);
  memset(buffer, 0, hid->report_length);

  // kprintf("hid: getting report descriptor\n");
  if (usb_run_ctrl_transfer(device, get_report, kheap_ptr_to_phys(buffer), hid->report_length) < 0) {
    kprintf("hid: failed to get report descriptor\n");
    return NULL;
  }

  // kprintf("hid: report descriptor loaded\n");
  return buffer;
}

int hid_get_idle(usb_device_t *device) {
  usb_setup_packet_t get_idle = GET_IDLE(0, 0);
  uint16_t *idle_rate = kmalloc(sizeof(uint16_t));

  hid_trace_debug("getting idle rate");
  // kprintf("hid: getting idle rate\n");
  if (usb_run_ctrl_transfer(device, get_idle, kheap_ptr_to_phys(idle_rate), sizeof(uint16_t)) < 0) {
    kprintf("hid: failed to get idle rate\n");
    return -1;
  }

  uint16_t idle = *idle_rate;
  hid_trace_debug("idle loaded");
  hid_trace_debug("idle: %d", idle);

  kfree(idle_rate);
  return idle;
}

int hid_set_idle(usb_device_t *device, uint8_t duration) {
  usb_setup_packet_t set_idle = SET_IDLE(duration, 0, 0);

  hid_trace_debug("setting idle rate");
  // kprintf("hid: setting idle rate to %d\n", duration);
  if (usb_run_ctrl_transfer(device, set_idle, 0, 0) < 0) {
    kprintf("hid: failed to set idle rate\n");
    return -1;
  }

  hid_trace_debug("idle rate set");
  // kprintf("hid: set idle rate\n");
  return 0;
}

//

int hid_device_init(usb_device_t *device) {
  kprintf("hid: initializing device\n");

  usb_if_descriptor_t *interface = device->interface;
  kassert(interface != NULL);

  hid_descriptor_t *desc = offset_ptr(interface, interface->length);
  // kprintf("hid descriptor:\n");
  // kprintf("  length = %d", desc->length);
  // kprintf("  type = %d\n", desc->type);
  // kprintf("  hid_ver = %X\n", desc->hid_ver);
  // kprintf("  num_descriptors = %d\n", desc->num_descriptors);
  // kprintf("  class_type = %d\n", desc->class_type);
  // kprintf("  report_length = %d\n", desc->report_length);

  void *report_desc = hid_get_report_descriptor(device, desc);
  if (report_desc == NULL) {
    kprintf("hid: failed to get report descriptor\n");
    return -1;
  }

  report_format_t *format = hid_parse_report_descriptor(report_desc, desc->report_length);
  if (format == NULL) {
    kprintf("hid: failed to parse report descriptor\n");
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
      hid_trace_debug("mouse");
      fn_ptr = hid_mouse_handle_input;
      data_ptr = hid_mouse_init(format);
      if (data_ptr == NULL) {
        hid_log("failed to initialize mouse driver");
        return -1;
      }
    } else if (usage == KEYBOARD_USAGE) {
      hid_trace_debug("keyboard");
      fn_ptr = hid_keyboard_handle_input;
      data_ptr = hid_keyboard_init(format);
      if (data_ptr == NULL) {
        hid_log("failed to initialize keyboard driver");
        return -1;
      }
    } else {
      hid_log("hid device not supported: %s", hid_get_usage_name(usage_page, usage));
      return -1;
    }
  } else {
    hid_log("hid device not supported: %s", hid_get_usage_name(usage_page, usage));
    return -1;
  }

  // set_idle sets the reporting rate of the interrupt endpoint
  // we set the device idle to 0 so that the endpoint only reports
  // when the device state changes
  hid_set_idle(device, 0);

  hid_device_t *hid = kmalloc(sizeof(hid_device_t));
  hid->desc = desc;
  hid->format = format;
  hid->buffer = hid_buffer_create(format->size);
  hid->size = format->size;
  hid->thread = NULL;
  hid->data = data_ptr;
  hid->handle_input = fn_ptr;

  device->driver_data = hid;
  hid->thread = thread_create(hid_device_event_loop, device);

  kprintf("hid: done\n");
  return 0;
}

int hid_device_deinit(usb_device_t *device) {
  // TODO: this
  return -1;
}
