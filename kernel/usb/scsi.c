//
// Created by Aaron Gill-Braun on 2021-04-22.
//

#include <usb/scsi.h>
#include <usb/usb.h>
#include <printf.h>
#include <mm.h>

void setup_command_block(usb_ms_cbw_t *cbw, void *cb, size_t size, size_t trnsf_len, bool dir) {
  memset(cbw, 0, sizeof(usb_ms_cbw_t));
  cbw->signature = CBW_SIGNATURE;
  cbw->tag = 1;
  cbw->flags = (dir << 7);
  cbw->trnsf_len = trnsf_len;
  cbw->lun = 0;
  cbw->cb_length = size;
  memcpy(cbw->cb, cb, size);
}

//

void *scsi_device_init(usb_device_t *dev) {
  kprintf("[scsi] init\n");
  scsi_device_t *device = kmalloc(sizeof(scsi_device_t));

  scsi_inquiry_cmd_t inquiry_cmd = {
    .op_code = SCSI_OP_INQUIRY,
    .evpd = 0,
    .page_code = 0,
    .alloc_length = sizeof(scsi_device_info_t),
    .control = 0,
  };

  usb_ms_cbw_t *cbw = kmalloc(sizeof(usb_ms_cbw_t));
  usb_ms_csw_t *csw = kmalloc(sizeof(usb_ms_csw_t));
  setup_command_block(cbw, &inquiry_cmd, sizeof(scsi_inquiry_cmd_t), sizeof(scsi_device_info_t), USB_IN);

  scsi_device_info_t *info = kmalloc(sizeof(scsi_device_info_t));
  memset(info, 0, sizeof(scsi_device_info_t));
  device->info = info;

  usb_add_transfer(dev, USB_OUT, (void *) heap_ptr_phys(cbw), sizeof(usb_ms_cbw_t));
  usb_add_transfer(dev, USB_IN, (void *) heap_ptr_phys(info), sizeof(scsi_device_info_t));
  usb_add_transfer(dev, USB_IN, (void *) heap_ptr_phys(csw), sizeof(usb_ms_csw_t));
  usb_start_transfer(dev, USB_OUT);
  usb_start_transfer(dev, USB_IN);
  usb_await_transfer(dev, USB_IN);

  kfree(cbw);
  kfree(csw);
  return device;
}

void scsi_handle_event(usb_event_t *event, void *data) {
  kprintf("[scsi] event\n");
}

// disk api

int scsi_read(usb_device_t *dev, uint64_t lba, uint32_t count, void **buf) {
  if (count == 0 || buf == NULL) {
    *buf = NULL;
    return 0;
  }

  kprintf("[scsi] read\n");

  uint64_t size = count * 512;
  scsi_read16_cmd_t read_cmd = {
    .op_code = SCSI_OP_READ_16,
    .dld2 = 0,
    .rarc = 0,
    .fua = 0,
    .dpo = 0,
    .rdprotect = 0,
    .lba = big_endian(lba),
    .xfer_length = big_endian(count),
    .group_num = 0,
    .dld0 = 0,
    .dld1 = 0,
    .control = 0
  };

  usb_ms_cbw_t *cbw = kmalloc(sizeof(usb_ms_cbw_t));
  usb_ms_csw_t *csw = kmalloc(sizeof(usb_ms_csw_t));
  setup_command_block(cbw, &read_cmd, sizeof(scsi_read16_cmd_t), size, USB_IN);

  page_t *buffer = alloc_zero_pages(SIZE_TO_PAGES(size), PE_WRITE);

  usb_add_transfer(dev, USB_OUT, (void *) heap_ptr_phys(cbw), sizeof(usb_ms_cbw_t));
  usb_add_transfer(dev, USB_IN, (void *) buffer->frame, size);
  usb_add_transfer(dev, USB_IN, (void *) heap_ptr_phys(csw), sizeof(usb_ms_csw_t));
  usb_start_transfer(dev, USB_OUT);
  usb_start_transfer(dev, USB_IN);
  usb_await_transfer(dev, USB_IN);

  kfree(cbw);
  kfree(csw);

  *buf = (void *) buffer->addr;
  return 0;
}


int scsi_write(usb_device_t *dev, uint64_t lba, uint32_t count, void **buf) {
  if (count == 0 || buf == NULL || *buf == NULL) {
    return 0;
  }

  page_t *buffer = vm_get_page((uintptr_t) *buf);
  if (buffer == NULL) {
    return -EINVAL;
  }

  uint64_t size = count * 512;
  scsi_write16_cmd_t write_cmd = {
    .op_code = SCSI_OP_WRITE_16,
    .dld2 = 0,
    .fua = 0,
    .dpo = 0,
    .wrprotect = 0,
    .lba = big_endian(lba),
    .xfer_length = big_endian(count),
    .group_num = 0,
    .dld0 = 0,
    .dld1 = 0,
    .control = 0
  };

  usb_ms_cbw_t *cbw = kmalloc(sizeof(usb_ms_cbw_t));
  usb_ms_csw_t *csw = kmalloc(sizeof(usb_ms_csw_t));
  setup_command_block(cbw, &write_cmd, sizeof(scsi_read16_cmd_t), size, USB_OUT);

  usb_add_transfer(dev, USB_OUT, (void *) heap_ptr_phys(cbw), sizeof(usb_ms_cbw_t));
  usb_add_transfer(dev, USB_OUT, (void *) buffer->frame, size);
  usb_add_transfer(dev, USB_IN, (void *) heap_ptr_phys(csw), sizeof(usb_ms_csw_t));
  usb_start_transfer(dev, USB_OUT);
  usb_start_transfer(dev, USB_IN);
  usb_await_transfer(dev, USB_OUT);

  kfree(cbw);
  kfree(csw);

  *buf = (void *) buffer->addr;
  return 0;
}
