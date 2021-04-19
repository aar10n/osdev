//
// Created by Aaron Gill-Braun on 2021-04-17.
//

#include <usb/hid.h>
#include <usb/hid-report.h>
#include <usb/xhci.h>
#include <printf.h>
#include <panic.h>

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

// void hid_get_report(xhci_device_t *device) {
//   usb_config_descriptor_t *config = device->configs[device->dev_config];
//   hid_descriptor_t *hid = get_hid_descriptor(device);
//
//   usb_setup_packet_t get_report = GET_REPORT(2, 0, device->dev_if, hid->report_length);
//   uint8_t *report = kmalloc(hid->report_length);
//
//   hid_trace_debug("getting report");
//   xhci_queue_setup(device, &get_report, SETUP_DATA_IN);
//   xhci_queue_data(device, (uintptr_t) report, hid->report_length, DATA_IN);
//   xhci_queue_status(device, DATA_OUT);
//   xhci_ring_device_db(device);
//
//   xhci_transfer_evt_trb_t *result = xhci_wait_for_transfer(device);
//   if (result->compl_code != CC_SUCCESS) {
//     hid_trace_debug("failed to get report");
//     return;
//   }
//   hid_trace_debug("report loaded");
// }

void hid_get_report_descriptor(xhci_device_t *device) {
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
    return;
  }
  hid_trace_debug("report descriptor loaded");

  // for (int i = 0; i < hid->report_length; i++) {
  //   kprintf("%#hhx ", report[i]);
  // }

  hid_parse_report_descriptor(report, hid->report_length);
}
