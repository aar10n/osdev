//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#ifndef KERNEL_USB_USB_H
#define KERNEL_USB_USB_H

#include <base.h>
#include <thread.h>

typedef struct xhci_device xhci_device_t;
typedef struct xhci_dev xhci_dev_t;


// Request Type
#define USB_GET_STATUS        0x0
#define USB_CLEAR_FEATURE     0x1
#define USB_SET_FEATURE       0x3
#define USB_SET_ADDRESS       0x5
#define USB_GET_DESCRIPTOR    0x6
#define USB_SET_DESCRIPTOR    0x7
#define USB_GET_CONFIGURATION 0x8
#define USB_SET_CONFIGURATION 0x9
#define USB_GET_INTERFACE     0xA

// Packet Request Type
#define USB_SETUP_TYPE_STANDARD 0
#define USB_SETUP_TYPE_CLASS    1
#define USB_SETUP_TYPE_VENDOR   2

// Packet Request Recipient
#define USB_SETUP_DEVICE    0
#define USB_SETUP_INTERFACE 1
#define USB_SETUP_ENDPOINT  2
#define USB_SETUP_OTHER     3

// Setup Packet Direction
#define USB_SETUP_HOST_TO_DEV 0
#define USB_SETUP_DEV_TO_HOST 1

// USB Setup Packet
typedef struct {
  struct {
    uint8_t recipient : 5;
    uint8_t type : 2;
    uint8_t direction : 1;
  } request_type;
  uint8_t request;
  uint16_t value;
  uint16_t index;
  uint16_t length;
} usb_setup_packet_t;
static_assert(sizeof(usb_setup_packet_t) == 8);

#define GET_DESCRIPTOR(t, i, l) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_DEVICE,            \
    .type = USB_SETUP_TYPE_STANDARD,          \
    .direction = USB_SETUP_DEV_TO_HOST,       \
  },                                          \
  .request = USB_GET_DESCRIPTOR,              \
  .value = ((t) << 8) | (i & 0xFF),           \
  .index = 0,                                 \
  .length = l,\
})

#define GET_INTERFACE(i) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_DEVICE,            \
    .type = USB_SETUP_TYPE_STANDARD,          \
    .direction = USB_SETUP_DEV_TO_HOST,       \
  },                                          \
  .request = USB_GET_INTERFACE,               \
  .value = 0,                                 \
  .index = i,                                 \
  .length = 1,                                \
})

#define SET_CONFIGURATION(c) ((usb_setup_packet_t){ \
  .request_type = {                           \
    .recipient = USB_SETUP_DEVICE,            \
    .type = USB_SETUP_TYPE_STANDARD,          \
    .direction = USB_SETUP_HOST_TO_DEV,       \
  },                                          \
  .request = USB_SET_CONFIGURATION,           \
  .value = c,                                 \
  .index = 0,                                 \
  .length = 0,                                \
})


//
// Device Classes
//

#define USB_CLASS_NONE     0x00 // use class information in interface descriptors
#define USB_CLASS_AUDIO    0x01 // audio devices
#define USB_CLASS_HID      0x03 // human interface devices
#define USB_CLASS_STORAGE  0x08 // mass storage devices
#define USB_CLASS_HUB      0x09 // usb hub devices

#define USB_SUBCLASS_SCSI  0x06

//
// Descriptors
//

#define DEVICE_DESCRIPTOR 0x1
#define CONFIG_DESCRIPTOR 0x2
#define IF_DESCRIPTOR     0x4
#define EP_DESCRIPTOR     0x5
#define STRING_DESCRIPTOR 0x3

// Generic Descriptor
typedef struct packed {
  uint8_t length;
  uint8_t type;
} usb_descriptor_t;

// Device Descriptor
typedef struct packed {
  uint8_t length;        // descriptor length
  uint8_t type;          // descriptor type (0x1)
  uint16_t usb_ver;      // usb version (bcd)
  uint8_t dev_class;     // device class code
  uint8_t dev_subclass;  // device subclass code
  uint8_t dev_protocol;  // device protocol code
  uint8_t max_packt_sz0; // max ep0 packet size
  uint16_t vendor_id;    // vendor id
  uint16_t product_id;   // product id
  uint16_t dev_release;  // device release number (bcd)
  uint8_t manuf_idx;     // index of manufacturer string
  uint8_t product_idx;   // index of product string
  uint8_t serial_idx;    // index of serial number
  uint8_t num_configs;   // number of configurations
} usb_device_descriptor_t;

// Configuration Descriptor
typedef struct packed {
  uint8_t length;     // descriptor length
  uint8_t type;       // descriptor type (0x2)
  uint16_t total_len; // total length of combined descriptors
  uint8_t num_ifs;    // number of interfaces
  uint8_t config_val; // configuration value (value to use in SET_CONFIGURATION request)
  uint8_t this_idx;   // own string descriptor index
  uint8_t attributes; // attributes bitmap
  uint8_t max_power;  // maximum power consumption
} usb_config_descriptor_t;

// Interface Association Descriptor
typedef struct packed {
  uint8_t length;      // descriptor length
  uint8_t type;        // descriptor type (0x4)
  uint8_t first_if;    // number of first interface
  uint8_t if_count;    // number of contiguous interfaces
  uint8_t fn_class;    // class code
  uint8_t fn_subclass; // subclass code
  uint8_t fn_protocol; // protocol code
  uint8_t this_idx;    // own string descriptor index
} usb_if_assoc_descriptor_t;

// Interface Descriptor
typedef struct packed {
  uint8_t length;      // descriptor length
  uint8_t type;        // descriptor type (0x4)
  uint8_t if_number;   // number of this interface
  uint8_t alt_setting; // value to select this alternate setting
  uint8_t num_eps;     // number of endpoints used
  uint8_t if_class;    // class code
  uint8_t if_subclass; // subclass code
  uint8_t if_protocol; // protocol code
  uint8_t this_idx;    // own string descriptor index
} usb_if_descriptor_t;

// Endpoint Descriptor
typedef struct packed {
  uint8_t length;       // descriptor length
  uint8_t type;         // descriptor type (0x5)
  uint8_t ep_addr;      // address of endpoint on device
  uint8_t attributes;   // attributes bitmap
  uint16_t max_pckt_sz; // maximum packet size
  uint8_t interval;     // interval for servicing
} usb_ep_descriptor_t;

// SuperSpeed Endpoint Companion Descriptor
typedef struct packed {
  uint8_t length;
  uint8_t type;
  uint8_t max_burst_sz;
  uint8_t attributes;
  uint16_t bytes_per_intvl;
} usb_ss_ep_descriptor_t;

// String Descriptor
typedef struct packed {
  // string descriptors use UNICODE UTF16LE encoding
  uint8_t length;    // size of string descriptor
  uint8_t type;      // descriptor type (0x2)
  char16_t string[]; // utf-16 string (size = length - 2)
} usb_string_t;

typedef struct packed {
  uint8_t length;         // descriptor length
  uint8_t type;           // descriptor type (0x2)
  usb_string_t strings[]; // individual string descriptors
} usb_string_descriptor_t;

//
//
//

typedef struct usb_dev usb_device_t;

typedef enum {
  USB_OUT,
  USB_IN,
} usb_dir_t;

typedef enum {
  TRANSFER_IN,
  TRANSFER_OUT,
} usb_event_type_t;

typedef enum {
  USB_SUCCESS,
  USB_ERROR,
} usb_status_t;

typedef struct usb_event {
  usb_device_t *device;  // device
  usb_event_type_t type; // event type
  usb_status_t status;   // event status
  time_t timestamp;      // timestamp
} usb_event_t;

typedef struct {
  const char *name;
  uint8_t dev_class;
  uint8_t dev_subclass;

  void *(*init)(usb_device_t *dev);
  void (*handle_event)(usb_event_t *event, void *data);
} usb_driver_t;

typedef struct usb_dev {
  id_t id;
  xhci_dev_t *hc;
  xhci_device_t *device;

  usb_driver_t *driver;
  void *driver_data;

  thread_t *thread;
} usb_device_t;


void usb_init();
void usb_register_device(xhci_device_t *device);
usb_device_t *usb_get_device(id_t id);

int usb_start_transfer(usb_device_t *dev, usb_dir_t dir);
int usb_add_transfer(usb_device_t *device, usb_dir_t dir, void *buffer, size_t size);
int usb_await_transfer(usb_device_t *device, usb_dir_t dir);

usb_ep_descriptor_t *usb_get_ep_descriptor(usb_if_descriptor_t *interface, uint8_t index);

#endif
