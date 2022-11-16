//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#ifndef KERNEL_USB_HID_H
#define KERNEL_USB_HID_H

#include <base.h>
#include <usb/usb.h>
#include <usb/xhci.h>
#include <usb/hid-report.h>

//
// Requests
//

#define HID_GET_REPORT   0x01
#define HID_GET_IDLE     0x02
#define HID_GET_PROTOCOL 0x03
#define HID_SET_REPORT   0x09
#define HID_SET_IDLE     0x0A
#define HID_SET_PROTOCOL 0x0B

#define GET_REPORT_DESCRIPTOR(l) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_INTERFACE,         \
    .type = USB_SETUP_DEVICE,                 \
    .direction = USB_SETUP_DEV_TO_HOST,       \
  },                                          \
  .request = USB_GET_DESCRIPTOR,              \
  .value = ((REPORT_DESCRIPTOR) << 8),        \
  .index = 0,                                 \
  .length = l,\
})

#define GET_REPORT(t, i, f, l) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_INTERFACE,         \
    .type = USB_SETUP_TYPE_CLASS,             \
    .direction = USB_SETUP_DEV_TO_HOST,       \
  },                                          \
  .request = HID_GET_REPORT,                  \
  .value = ((t) << 8) | (i & 0xFF),           \
  .index = f,                                 \
  .length = l,\
})

#define SET_REPORT(t, i, f, l) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_INTERFACE,         \
    .type = USB_SETUP_TYPE_CLASS,             \
    .direction = USB_SETUP_HOST_TO_DEV,       \
  },                                          \
  .request = HID_SET_REPORT,                  \
  .value = ((t) << 8) | (i & 0xFF),           \
  .index = f,                                 \
  .length = l,\
})

#define GET_IDLE(i, f) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_INTERFACE,         \
    .type = USB_SETUP_TYPE_CLASS,             \
    .direction = USB_SETUP_DEV_TO_HOST,       \
  },                                          \
  .request = HID_GET_IDLE,                    \
  .value = (i & 0xFF),                        \
  .index = f,                                 \
  .length = 1,\
})

#define SET_IDLE(d, i, f) ((usb_setup_packet_t){ \
  .request_type = {                       \
    .recipient = USB_SETUP_INTERFACE,     \
    .type = USB_SETUP_TYPE_CLASS,         \
    .direction = USB_SETUP_HOST_TO_DEV    \
  },                                      \
  .request = HID_SET_IDLE,                \
  .value = ((d) << 8) | (i & 0xFF),       \
  .index = f,                             \
  .length = 0,                            \
})

//
// Descriptors
//

#define REPORT_DESCRIPTOR 0x22

typedef struct packed {
  uint8_t length;
  uint8_t type;
  uint16_t hid_ver;
  uint8_t country_code;
  uint8_t num_descriptors;
  uint8_t class_type;
  uint16_t report_length;
} hid_descriptor_t;

//

typedef struct {
  uintptr_t alloc_ptr;
  uintptr_t read_ptr;
  uint16_t alloc_size;
  uint16_t max_index;
  page_t *page;
} hid_buffer_t;

typedef struct hid_device {
  hid_descriptor_t *desc;
  report_format_t *format;
  hid_buffer_t *buffer;
  size_t size;

  void *data;
  void (*handle_input)(struct hid_device *device, uint8_t *buffer);
} hid_device_t;


int hid_device_init(usb_device_t *device);
int hid_device_deinit(usb_device_t *device);
int hid_device_handle_event(usb_event_t *event);

// void *hid_device_init(usb_dev_t *dev);
// void hid_handle_event(usb_event_t *event, void *data);
//
// void hid_get_idle(xhci_device_t *device);
// void hid_set_idle(xhci_device_t *device, uint8_t duration);

#endif
