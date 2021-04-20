//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/usb.h>
#include <usb/xhci.h>
#include <printf.h>

#define HID_DEBUG
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

// device specific



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

  hid_device_t *hid = kmalloc(sizeof(hid_device_t));
  hid->desc = desc;
  hid->format = format;

  hid->buffer = kmalloc(8);
  hid->size = 8;
  memset(hid->buffer, 0, 8);

  usb_add_transfer(dev, USB_IN, (void *) heap_ptr_phys(hid->buffer), 8);
  return hid;
}

void hid_handle_event(usb_event_t *event, void *data) {
  usb_device_t *usb_dev = event->device;
  hid_device_t *device = data;
  hid_trace_debug("event");

  uint8_t buffer[device->size];
  memcpy(buffer, device->buffer, device->size);
  usb_add_transfer(usb_dev, USB_IN, (void *) heap_ptr_phys(device->buffer), device->size);

  for (int i = 0; i < device->size; i++) {
    kprintf("%#x ", (int) buffer[i]);
  }
  kprintf("\n");
  int x = 5;
}
