//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/hid-usage.h>
#include <usb/keyboard.h>
#include <usb/mouse.h>
#include <usb/usb.h>
#include <usb/xhci.h>
#include <printf.h>
#include <event.h>

#define hid_log(str, args...) kprintf("[hid] " str "\n", ##args)

// #define HID_DEBUG
#ifdef HID_DEBUG
#define hid_trace_debug(str, args...) kprintf("[hid] " str "\n", ##args)
#else
#define hid_trace_debug(str, args...)
#endif

static inline hid_descriptor_t *get_hid_descriptor(xhci_device_t *device) {
  usb_config_descriptor_t *config = device->configs[device->dev_config];
  usb_if_descriptor_t *interface = offset_ptr(config, config->length);
  return offset_ptr(interface, interface->length);
}

// HID Buffers

hid_buffer_t *hid_buffer_create(uint16_t alloc_size) {
  hid_buffer_t *buffer = kmalloc(sizeof(hid_buffer_t));
  page_t *page = alloc_zero_page(PE_WRITE);

  buffer->alloc_ptr = page->frame;
  buffer->read_ptr = page->addr;
  buffer->alloc_size = alloc_size;
  buffer->max_index = PAGE_SIZE / alloc_size;
  buffer->page = page;
  return buffer;
}

uintptr_t hid_buffer_alloc(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->alloc_ptr;
  int index = (ptr - buffer->page->frame) / buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->alloc_ptr = buffer->page->frame;
  } else {
    buffer->alloc_ptr += buffer->alloc_size;
  }
  return ptr;
}

void *hid_buffer_read(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  int index = (ptr - buffer->page->addr) / buffer->alloc_size;
  if (index == buffer->max_index - 1) {
    buffer->read_ptr = buffer->page->addr;
  } else {
    buffer->read_ptr += buffer->alloc_size;
  }
  return (void *) ptr;
}

void *hid_buffer_read_last(hid_buffer_t *buffer) {
  uintptr_t ptr = buffer->read_ptr;
  if (ptr == buffer->page->addr) {
    return (void *) buffer->page->addr + PAGE_SIZE - buffer->alloc_size;
  }
  return (void *) ptr - buffer->alloc_size;
}

//

void *hid_get_report_descriptor(xhci_device_t *device) {
  hid_descriptor_t *hid = get_hid_descriptor(device);
  usb_setup_packet_t get_report = GET_REPORT_DESCRIPTOR(hid->report_length);
  uint8_t *report = kmalloc(hid->report_length);

  hid_trace_debug("getting report descriptor");
  xhci_queue_setup(device, &get_report, SETUP_DATA_IN);
  xhci_queue_data(device, (uintptr_t) report, hid->report_length, DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_ring_device_db(device);

  xhci_transfer_evt_trb_t *result = xhci_wait_for_transfer(device);
  if (result->compl_code != CC_SUCCESS) {
    hid_trace_debug("failed to get report descriptor");
    return NULL;
  }
  hid_trace_debug("report descriptor loaded");
  return report;
}

void hid_get_idle(xhci_device_t *device) {
  usb_setup_packet_t get_idle = GET_IDLE(0, 0);
  uint16_t idle_rate = 0;

  hid_trace_debug("getting idle rate");
  xhci_queue_setup(device, &get_idle, SETUP_DATA_IN);
  xhci_queue_data(device, (uintptr_t) &idle_rate, sizeof(uint16_t), DATA_IN);
  xhci_queue_status(device, DATA_OUT);
  xhci_ring_device_db(device);

  xhci_transfer_evt_trb_t *result = xhci_wait_for_transfer(device);
  if (result->compl_code != CC_SUCCESS) {
    hid_trace_debug("failed to get idle");
    return;
  }
  hid_trace_debug("idle loaded");
  hid_trace_debug("idle: %d", idle_rate);
}

void hid_set_idle(xhci_device_t *device, uint8_t duration) {
  usb_setup_packet_t set_idle = SET_IDLE(duration, 0, 0);

  hid_trace_debug("setting idle rate");
  xhci_queue_setup(device, &set_idle, SETUP_DATA_OUT);
  xhci_queue_status(device, DATA_OUT);
  xhci_ring_device_db(device);

  xhci_transfer_evt_trb_t *result = xhci_wait_for_transfer(device);
  if (result->compl_code != CC_SUCCESS) {
    hid_trace_debug("failed to set idle rate");
    return;
  }
  hid_trace_debug("idle rate set");
}

//

void *hid_device_init(usb_device_t *dev) {
  hid_descriptor_t *desc = get_hid_descriptor(dev->device);
  void *report_desc = hid_get_report_descriptor(dev->device);
  if (report_desc == NULL) {
    return NULL;
  }

  report_format_t *format = hid_parse_report_descriptor(report_desc, desc->report_length);
  if (format == NULL) {
    kfree(report_desc);
    return NULL;
  }

  kprintf(">>> setting device mode\n");
  dev->mode = USB_DEVICE_POLLING;
  dev->value = 16000;
  // dev->value = 500000;

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
      // hid_set_idle(dev->device, 0);
      if (data_ptr == NULL) {
        hid_log("failed to initialize mouse driver");
        return NULL;
      }
    } else if (usage == KEYBOARD_USAGE) {
      hid_trace_debug("keyboard");
      fn_ptr = hid_keyboard_handle_input;
      data_ptr = hid_keyboard_init(format);
      if (data_ptr == NULL) {
        hid_log("failed to initialize keyboard driver");
        return NULL;
      }
    } else {
      hid_log("hid device not supported: %s", hid_get_usage_name(usage_page, usage));
      return NULL;
    }

  } else {
    hid_log("hid device not supported: %s", hid_get_usage_name(usage_page, usage));
    return NULL;
  }

  hid_device_t *hid = kmalloc(sizeof(hid_device_t));
  hid->desc = desc;
  hid->format = format;
  hid->buffer = hid_buffer_create(format->size);
  hid->size = format->size;

  hid->data = data_ptr;
  hid->handle_input = fn_ptr;

  for (int i = 0; i < 8; i++) {
    usb_add_transfer(dev, USB_IN, (void *) hid_buffer_alloc(hid->buffer), format->size);
  }
  return hid;
}

void hid_handle_event(usb_event_t *event, void *data) {
  usb_device_t *usb_dev = event->device;
  hid_device_t *device = data;
  hid_trace_debug("event");

  uint8_t *buffer = hid_buffer_read(device->buffer);
  usb_add_transfer(usb_dev, USB_IN, (void *) hid_buffer_alloc(device->buffer), device->size);
  device->handle_input(device, buffer);
}
