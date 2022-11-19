//
// Created by Aaron Gill-Braun on 2021-04-22.
//

#ifndef KERNEL_USB_SCSI_H
#define KERNEL_USB_SCSI_H

#include <base.h>
#include <usb/usb.h>

//
// MARK: USB
//

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

#define CS_PASSED    0
#define CS_FAILED    1
#define CS_PHASE_ERR 2

// Command Block Wrapper
typedef struct packed {
  uint32_t signature;    // signature
  uint32_t tag;          // tag
  uint32_t trnsf_len;    // transfer length
  uint8_t flags;         // flags (direction, 0 = out | 1 = in)
  uint8_t lun : 4;       // device logical unit number
  uint8_t : 4;           // reserved
  uint8_t cb_length : 5; // command block length
  uint8_t : 3;           // reserved
  uint8_t cb[16];        // command block
} usb_ms_cbw_t;
static_assert(sizeof(usb_ms_cbw_t) == 31);

// Command Status Wrapper
typedef struct packed {
  uint32_t signature; // signature
  uint32_t tag;       // tag
  uint32_t residue;   // data residue (expected - received)
  uint8_t status;     // status
} usb_ms_csw_t;
static_assert(sizeof(usb_ms_csw_t) == 13);

//
// Commands
//

#define SCSI_OP_INQUIRY 0x12
#define SCSI_OP_READ_10 0x28
#define SCSI_OP_READ_12 0xA8
#define SCSI_OP_READ_16 0x88
#define SCSI_OP_WRITE_10 0x2A
#define SCSI_OP_WRITE_12 0xAA
#define SCSI_OP_WRITE_16 0x8A


typedef struct packed {
  uint8_t op_code;        // operation code
  uint8_t evpd : 1;       // enable vital product data
  uint8_t : 7;            // reserved
  uint8_t page_code;      // page code
  uint16_t alloc_length;  // allocation length (min 5)
  uint8_t control;        // control
} scsi_inquiry_cmd_t;

// read commands

// read (10) = 32-bit lba | 16-bit transfer length | code = 0x28
typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t : 2;           // reserved
  uint8_t rarc : 1;      // rebuild assist recovery control
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t rdprotect : 3; // read protect
  uint32_t lba;          // logical block address (big endian)
  uint8_t group_num : 5; // group number
  uint8_t : 3;           // reserved
  uint16_t xfer_length;  // transfer length (big endian)
  uint8_t control;       // control
} scsi_read10_cmd_t;
static_assert(sizeof(scsi_read10_cmd_t) == 10);

// read (12) = 32-bit lba | 32-bit transfer length | code = 0xA8
typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t : 2;           // reserved
  uint8_t rarc : 1;      // rebuild assist recovery control
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t rdprotect : 3; // read protect
  uint32_t lba;          // logical block address (big endian)
  uint32_t xfer_length;  // transfer length (big endian)
  uint8_t group_num : 5; // group number
  uint8_t : 3;           // reserved
  uint8_t control;       // control
} scsi_read12_cmd_t;
static_assert(sizeof(scsi_read12_cmd_t) == 12);

// read (16) = 64-bit lba | 32-bit transfer length | code = 0x88
typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t dld2 : 1;      // duration limit descriptor
  uint8_t : 1;           // reserved
  uint8_t rarc : 1;      // rebuild assist recovery control
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t rdprotect : 3; // read protect
  uint64_t lba;          // logical block address (big endian)
  uint32_t xfer_length;  // transfer length (big endian)
  uint8_t group_num : 6; // group number
  uint8_t dld0 : 1;      // duration limit descriptor
  uint8_t dld1 : 1;      // duration limit descriptor
  uint8_t control;       // control
} scsi_read16_cmd_t;
static_assert(sizeof(scsi_read16_cmd_t) == 16);

// write commands

typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t : 3;           // reserved
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t wrprotect : 3; // write protect
  uint32_t lba;          // logical block address (big endian)
  uint8_t group_num : 5; // group number
  uint8_t : 3;           // reserved
  uint16_t xfer_length;  // transfer length (big endian)
  uint8_t control;       // control
} scsi_write10_cmd_t;
static_assert(sizeof(scsi_write10_cmd_t) == 10);

typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t : 3;           // reserved
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t wrprotect : 3; // write protect
  uint32_t lba;          // logical block address (big endian)
  uint32_t xfer_length;  // transfer length (big endian)
  uint8_t group_num : 5; // group number
  uint8_t : 3;           // reserved
  uint8_t control;       // control
} scsi_write12_cmd_t;
static_assert(sizeof(scsi_write12_cmd_t) == 12);

typedef struct packed {
  uint8_t op_code;       // operation code
  uint8_t dld2 : 1;      // duration limit descriptor
  uint8_t : 2;           // reserved
  uint8_t fua : 1;       // force unit access
  uint8_t dpo : 1;       // disable page out
  uint8_t wrprotect : 3; // write protect
  uint64_t lba;          // logical block address (big endian)
  uint32_t xfer_length;  // transfer length (big endian)
  uint8_t group_num : 6; // group number
  uint8_t dld0 : 1;      // duration limit descriptor
  uint8_t dld1 : 1;      // duration limit descriptor
  uint8_t control;       // control
} scsi_write16_cmd_t;
static_assert(sizeof(scsi_write16_cmd_t) == 16);


//
//
//

typedef struct {
  uint8_t dev_type : 5;     // peripheral device type
  uint8_t qualifier : 3;    // peripheral qualifier
  //
  uint8_t : 7;               // reserved
  uint8_t rmb : 1;           // removable media
  //
  uint8_t version;           // version
  //
  uint8_t format : 4;        // response data format
  uint8_t hisup : 1;         // hierarchical support
  uint8_t normaca : 1;       // normal aca supported
  uint8_t : 2;               // reserved
  //
  uint8_t extra_length;      // additional length (n - 4)
  //
  uint8_t protect : 1;       // protect
  uint8_t : 2;               // reserved
  uint8_t pc3 : 1;           // third-party copy
  uint8_t tpgs : 2;          // target port group support
  uint8_t acc : 1;           // access controls coordinator
  uint8_t scss : 1;          // scc supported
  //
  uint8_t : 4;               // reserved
  uint8_t multip : 1;        // multi port
  uint8_t : 1;               // reserved
  uint8_t encserv : 1;       // enclosure services
  uint8_t : 1;               // reserved
  //
  uint8_t : 1;               // reserved
  uint8_t cmdque : 1;        // command queuing
  uint8_t : 6;               // reserved
  //
  uint8_t vendor_id[8];      // T10 vendor identification
  uint8_t product_id[16];    // product identification
  uint8_t product_rev[4];    // product revision level
  uint8_t serial_num[8];     // drive serial number
} scsi_device_info_t;

typedef struct scsi_device {
  scsi_device_info_t *info;
} scsi_device_t;


// MARK: USB Driver API
int scsi_device_init(usb_device_t *device);
int scsi_device_deinit(usb_device_t *device);

ssize_t scsi_read(usb_device_t *device, uint64_t lba, uint32_t count, void *buf);
ssize_t scsi_write(usb_device_t *device, uint64_t lba, uint32_t count, void *buf);

#endif
