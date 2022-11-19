//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#ifndef KERNEL_USB_USB_H
#define KERNEL_USB_USB_H

#include <base.h>
#include <thread.h>
#include <chan.h>

typedef struct pcie_device pcie_device_t;

// USB Device Mode
#define USB_DEVICE_REGULAR    0x0
#define USB_DEVICE_POLLING    0x1

// USB Transfer Flags
#define USB_XFER_SETUP  0x1 // transfer is a setup transfer
#define USB_XFER_PART   0x2 // transfer is not the last in a series

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
typedef struct packed usb_setup_packet {
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
#define STRING_DESCRIPTOR 0x3
#define IF_DESCRIPTOR     0x4
#define EP_DESCRIPTOR     0x5

#define cast_usb_desc(ptr) (((usb_descriptor_t *)((void *)(ptr))))

// Generic Descriptor
typedef struct packed usb_descriptor {
  uint8_t length;
  uint8_t type;
} usb_descriptor_t;

// Device Descriptor
typedef struct packed usb_device_descriptor {
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
static_assert(sizeof(usb_device_descriptor_t) == 18);

// Configuration Descriptor
typedef struct packed usb_config_descriptor {
  uint8_t length;     // descriptor length
  uint8_t type;       // descriptor type (0x2)
  uint16_t total_len; // total length of combined descriptors
  uint8_t num_ifs;    // number of interfaces
  uint8_t config_val; // configuration value (value to use in SET_CONFIGURATION request)
  uint8_t this_idx;   // own string descriptor index
  uint8_t attributes; // attributes bitmap
  uint8_t max_power;  // maximum power consumption
} usb_config_descriptor_t;
static_assert(sizeof(usb_config_descriptor_t) == 9);

// Interface Association Descriptor
typedef struct packed usb_if_assoc_descriptor {
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
typedef struct packed usb_if_descriptor {
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
static_assert(sizeof(usb_if_descriptor_t) == 9);

#define USB_EP_OUT 0
#define USB_EP_IN  1

// Endpoint Descriptor
typedef struct packed usb_ep_descriptor {
  uint8_t length;       // descriptor length
  uint8_t type;         // descriptor type (0x5)
  uint8_t ep_addr;      // address of endpoint on device
  uint8_t attributes;   // attributes bitmap
  uint16_t max_pckt_sz; // maximum packet size
  uint8_t interval;     // interval for servicing
} usb_ep_descriptor_t;
static_assert(sizeof(usb_ep_descriptor_t) == 7);

// SuperSpeed Endpoint Companion Descriptor
typedef struct packed usb_ss_ep_descriptor {
  uint8_t length;
  uint8_t type;
  uint8_t max_burst_sz;
  uint8_t attributes;
  uint16_t bytes_per_intvl;
} usb_ss_ep_descriptor_t;

// String Descriptor
typedef struct packed usb_string {
  // string descriptors use UNICODE UTF16LE encoding
  uint8_t length;    // size of string descriptor
  uint8_t type;      // descriptor type (0x2)
  char16_t string[]; // utf-16 string (size = length - 2)
} usb_string_t;

typedef struct packed usb_string_descriptor {
  uint8_t length;         // descriptor length
  uint8_t type;           // descriptor type (0x2)
  usb_string_t strings[]; // individual string descriptors
} usb_string_descriptor_t;

//
// MARK: Common API
//

typedef struct usb_device usb_device_t;
typedef struct usb_endpoint usb_endpoint_t;
typedef struct usb_host usb_host_t;
typedef struct usb_hub usb_hub_t;
typedef struct usb_transfer usb_transfer_t;
typedef struct usb_event usb_event_t;

typedef enum usb_dir {
  USB_OUT,
  USB_IN,
} usb_dir_t;

typedef enum usb_status {
  USB_SUCCESS,
  USB_ERROR,
} usb_status_t;

typedef enum usb_xfer_type {
  // TODO: change to USB_CONTROL_XFER
  USB_SETUP_XFER,
  USB_DATA_IN_XFER,
  USB_DATA_OUT_XFER
} usb_xfer_type_t;

typedef enum usb_event_type {
  USB_CTRL_EV,
  USB_IN_EV,
  USB_OUT_EV,
} usb_event_type_t;

typedef enum usb_ep_type {
  USB_CONTROL_EP,
  USB_ISOCHRONOUS_EP,
  USB_BULK_EP,
  USB_INTERRUPT_EP,
} usb_ep_type_t;

typedef enum usb_device_mode {
  USB_REGULAR_MODE,
  USB_POLLING_MODE,
} usb_device_mode_t;

typedef enum usb_revision {
  USB_REV_2_0,
  USB_REV_3_0,
  USB_REV_3_1,
  USB_REV_3_2,
} usb_revision_t;

typedef enum usb_speed {
  USB_FULL_SPEED,
  USB_LOW_SPEED,
  USB_HIGH_SPEED,
  USB_SUPER_SPEED_G1X1,
  USB_SUPER_SPEED_G2X1,
  USB_SUPER_SPEED_G1X2,
  USB_SUPER_SPEED_G2X2,
} usb_speed_t;

typedef struct usb_host_impl {
  /* controller */
  int (*init)(usb_host_t *host);
  int (*start)(usb_host_t *host);
  int (*stop)(usb_host_t *host);
  int (*discover)(usb_host_t *host);
} usb_host_impl_t;

typedef struct usb_device_impl {
  /* device */
  int (*init)(usb_device_t *device);
  int (*deinit)(usb_device_t *device);
  int (*add_transfer)(usb_device_t *device, usb_endpoint_t *endpoint, usb_transfer_t *transfer);
  int (*start_transfer)(usb_device_t *device, usb_endpoint_t *endpoint);
  int (*await_event)(usb_device_t *device, usb_endpoint_t *endpoint, usb_event_t *event);
  int (*read_device_descriptor)(usb_device_t *device, usb_device_descriptor_t **out);
  /* endpoints */
  int (*init_endpoint)(usb_endpoint_t *endpoint);
  int (*deinit_endpoint)(usb_endpoint_t *endpoint);
} usb_device_impl_t;

typedef struct usb_driver {
  const char *name;
  uint8_t dev_class;
  uint8_t dev_subclass;

  int (*init)(usb_device_t *device);
  int (*deinit)(usb_device_t *device);
} usb_driver_t;

typedef struct usb_hub {
  uint8_t port;
  uint8_t tier;

  usb_device_t *self;
  usb_host_t *host;
  void *data;

  size_t num_devices;
  LIST_HEAD(usb_device_t) devices;
} usb_hub_t;

#define USB_DEVICE_RO   0x1 // device is removable
#define USB_DEVICE_HUB  0x2 // device is a usb hub

typedef struct usb_device {
  uint8_t port;
  uint8_t dev_class;
  uint8_t dev_subclass;
  uint8_t dev_protocol;
  uint32_t flags;

  usb_revision_t revision;
  usb_speed_t speed;
  usb_device_mode_t mode;

  usb_device_descriptor_t *desc;
  usb_config_descriptor_t **configs;

  char *product;
  char *manufacturer;
  char *serial;

  usb_config_descriptor_t *config;     // selected config
  usb_if_descriptor_t **interfaces;    // interfaces for selected config
  usb_if_descriptor_t *interface;      // selected interface
  LIST_HEAD(usb_endpoint_t) endpoints; // endpoints for selected interface

  usb_host_t *host;
  usb_hub_t *parent;
  void *host_data;

  usb_driver_t *driver;
  void *driver_data;

  LIST_ENTRY(struct usb_device) list;
} usb_device_t;

typedef struct usb_endpoint {
  usb_ep_type_t type;
  usb_dir_t dir;
  uint8_t number;
  uint8_t attributes;
  uint16_t max_pckt_sz;
  uint8_t interval;

  usb_device_t *device;
  void *host_data;
  chan_t *event_ch;

  LIST_ENTRY(struct usb_endpoint) list;
} usb_endpoint_t;

typedef struct usb_host {
  char *name;
  void *data;
  pcie_device_t *pci_device;
  usb_host_impl_t *host_impl;
  usb_device_impl_t *device_impl;

  usb_hub_t *root;
  LIST_ENTRY(struct usb_host) list;
} usb_host_t;

typedef struct usb_transfer {
  usb_xfer_type_t type;
  uint32_t flags;

  uintptr_t buffer;
  size_t length;

  union {
    usb_setup_packet_t setup;
    usb_transfer_t *next;
    uint64_t raw;
  };
} usb_transfer_t;

typedef struct usb_event {
  usb_event_type_t type;
  usb_status_t status;
} usb_event_t;
static_assert(sizeof(usb_event_t) <= 8);

void usb_init();

// MARK: Host Driver API
int usb_register_host(usb_host_t *host);
int usb_handle_device_connect(usb_host_t *host, void *data);
int usb_handle_device_disconnect(usb_host_t *host, usb_device_t *device);

// MARK: Common API
int usb_run_ctrl_transfer(usb_device_t *device, usb_setup_packet_t setup, uintptr_t buffer, size_t length);
int usb_add_transfer(usb_device_t *device, usb_dir_t direction, uintptr_t buffer, size_t length);
int usb_start_transfer(usb_device_t *device, usb_dir_t direction);
int usb_await_transfer(usb_device_t *device, usb_dir_t direction);
int usb_start_await_transfer(usb_device_t *device, usb_dir_t direction);

void usb_print_device_descriptor(usb_device_descriptor_t *desc);
void usb_print_config_descriptor(usb_config_descriptor_t *desc);

// MARK: Internal API
int usb_device_init(usb_device_t *device);
int usb_device_configure(usb_device_t *device, usb_config_descriptor_t *config, usb_if_descriptor_t *interface);
int usb_device_free_endpoints(usb_device_t *device);
usb_config_descriptor_t *usb_device_read_config_descriptor(usb_device_t *device, uint8_t n);
char *usb_device_read_string(usb_device_t *device, uint8_t n);

#endif
