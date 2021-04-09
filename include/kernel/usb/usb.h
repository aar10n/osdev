//
// Created by Aaron Gill-Braun on 2021-04-03.
//

#ifndef KERNEL_USB_USB_H
#define KERNEL_USB_USB_H

#include <base.h>

// Request Type
#define USB_CLEAR_FEATURE     1
#define USB_GET_DESCRIPTOR    6
#define USB_GET_CONFIGURATION 8

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
#define USB_SETUP_HOST_2_DEV 0
#define USB_SETUP_DEV_2_HOST 1

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
} usb_packet_setup_t;
static_assert(sizeof(usb_packet_setup_t) == 8);


#endif
