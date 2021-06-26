//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef DRIVERS_AHCI_H
#define DRIVERS_AHCI_H

#include <base.h>
#include <device.h>
#include <bus/pcie.h>

#define	SATA_SIG_ATA    0x00000101	// SATA drive
#define	SATA_SIG_ATAPI  0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB   0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	    0x96690101	// Port multiplier

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM 3
#define AHCI_DEV_SATAPI 4

#define	AHCI_BASE	0x400000	// 4M

#define HBA_PxCMD_ST  (1 << 0)  // ST   - start (command processing)
#define HBA_PxCMD_SUD (1 << 1)  // SUD  - spin-up device
#define HBA_PxCMD_FRE (1 << 4)  // FRE  - FIS receive enable
#define HBA_PxCMD_FR  (1 << 14) // FR   - FIS receive running
#define HBA_PxCMD_CR  (1 << 15) // CR   - command list running
#define HBA_PxIS_TFES (1 << 30) // TFES - task file error status

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define HBA_CTRL_RESET       (1 << 0)
#define HBA_CTRL_INT_ENABLE  (1 << 1)
#define HBA_CTRL_AHCI_ENABLE (1 << 31)

typedef enum {
  ATA_CMD_READ_DMA_EXT = 0x25,
  ATA_CMD_READ_DMA_QUEUED = 0x26,
  ATA_CMD_WRITE_DMA_EXT = 0x35,
  ATA_CMD_WRITE_DMA_QUEUED_EXT = 0x36,
  ATA_CMD_IDENTIFY_PACKET_DEV = 0xA1,
  ATA_CMD_IDENTIFY = 0xEC,
} ata_command_t;

//
// FIS Packets
//

typedef enum {
  FIS_TYPE_REG_H2D   = 0x27, // register FIS - host to device
  FIS_TYPE_REG_D2H   = 0x34, // register FIS - device to host
  FIS_TYPE_DMA_ACT   = 0x39, // DMA activate FIS - device to host
  FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
  FIS_TYPE_DATA      = 0x46, // data FIS - bidirectional
  FIS_TYPE_BIST      = 0x58, // BIST activate FIS - bidirectional
  FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
  FIS_TYPE_DEV_BITS  = 0xA1, // set device bits FIS - device to host
} fis_type_t;

// host to device register FIS
typedef struct packed {
  // dword 0
  uint32_t fis_type : 8;     // fis_type_t
  uint32_t pmport : 4;       // port multiplier
  uint32_t : 3;              // reserved
  uint32_t cmd_ctrl : 1;     // 1: command 0: control

  uint32_t command :  8;     // command register
  uint32_t feature_low : 8;  // feature register 7:0

  // dword 1
  uint32_t lba0 : 8;         // LBA low register 7:0
  uint32_t lba1 : 8;         // LBA mid register 15:8
  uint32_t lba2 : 8;         // LBA high register 23:16
  uint32_t device : 8;       // device register

  // dword 2
  uint32_t lba3 : 8;         // LBA register 31:24
  uint32_t lba4 : 8;         // LBA register 39:32
  uint32_t lba5 : 8;         // LBA register 47:40
  uint32_t feature_high : 8; // feature register 15:8

  // dword 3
  uint32_t count_low : 8;    // count register 7:0
  uint32_t count_high : 8;   // count register 15:8
  uint32_t icc : 8;          // isochronous command completion
  uint32_t control : 8;      // control register

  // dword 4
  uint32_t : 32;             // reserved
} fis_reg_h2d_t;
static_assert(sizeof(fis_reg_h2d_t) == sizeof(uint32_t) * 5);

// device to host register FIS
typedef struct packed {
  // dword 0
  uint32_t fis_type : 8;    // fis_type_t
  uint32_t pmport : 4;      // port multiplier
  uint32_t : 2;             // reserved
  uint32_t interrupt : 1;   // interrupt bit
  uint32_t : 1;             // reserved

  uint32_t status :  8;     // status register
  uint32_t error : 8;       // error register

  // dword 1
  uint32_t lba0 : 8;        // LBA low register 7:0
  uint32_t lba1 : 8;        // LBA mid register 15:8
  uint32_t lba2 : 8;        // LBA high register 23:16
  uint32_t device : 8;      // device register

  // dword 2
  uint32_t lba3 : 8;        // LBA register 31:24
  uint32_t lba4 : 8;        // LBA register 39:32
  uint32_t lba5 : 8;        // LBA register 47:40
  uint32_t : 8;             // reserved

  // dword 3
  uint32_t count_low : 8;   // count register 7:0
  uint32_t count_high : 8;  // rount register 15:8
  uint32_t : 16;            // reserved

  // dword 4
  uint32_t : 32;            // reserved
} fis_reg_d2h_t;
static_assert(sizeof(fis_reg_d2h_t) == sizeof(uint32_t) * 5);

// data FIS
typedef struct {
  // dword 0
  uint32_t fis_type : 8; // fis_type_t
  uint32_t pmport : 4;   // port multiplier
  uint32_t : 4;          // reserved
  uint32_t : 16;         // reserved

  // dword 1 - n
  uint32_t data;         // variable size
} fis_data_t;

// pio setup - device to host FIS
typedef struct packed {
  // dword 0
  uint32_t fis_type : 8;      // fis_type_t
  uint32_t pmport : 4;        // port multiplier
  uint32_t : 1;               // reserved
  uint32_t transf_dir : 1;    // data transfer direction
  uint32_t interrupt : 1;     // interrupt bit
  uint32_t : 1;               // reserved

  uint32_t status :  8;       // status register
  uint32_t error : 8;         // error register

  // dword 1
  uint32_t lba0 : 8;          // LBA low register 7:0
  uint32_t lba1 : 8;          // LBA mid register 15:8
  uint32_t lba2 : 8;          // LBA high register 23:16
  uint32_t device : 8;        // device register

  // dword 2
  uint32_t lba3 : 8;          // LBA register 31:24
  uint32_t lba4 : 8;          // LBA register 39:32
  uint32_t lba5 : 8;          // LBA register 47:40
  uint32_t : 8;               // reserved

  // dword 3
  uint32_t count_low : 8;     // count register 7:0
  uint32_t count_high : 8;    // rount register 15:8
  uint32_t : 8;               // reserved
  uint32_t e_status : 8;      // new value of status register

  // dword 4
  uint32_t transf_count : 16; // transfer count
  uint32_t : 16;              // reserved
} fis_pio_setup_t;
static_assert(sizeof(fis_pio_setup_t) == sizeof(uint32_t) * 5);

// dma setup - device to host FIS
typedef struct packed {
  // dword 0
  uint32_t fis_type : 8;      // fis_type_t
  uint32_t pmport : 4;        // port multiplier
  uint32_t : 1;               // reserved
  uint32_t transf_dir : 1;    // data transfer direction
  uint32_t interrupt : 1;     // interrupt bit
  uint32_t auto_activate: 1;  // auto-activate
  uint32_t : 16;              // reserved

  // dword 1 & 2
  uint64_t dma_buffer_id;     // DMA buffer identifier

  // dword 3
  uint32_t : 32;              // reserved

  // dword 4
  uint32_t dma_buffer_offset; // DMA buffer offset

  // dword 5
  uint32_t transf_count;      // number of bytes to transfer (bit 0 must be 0)

  // dword 6
  uint32_t : 32;              // reserved
} fis_dma_setup_t;
static_assert(sizeof(fis_dma_setup_t) == sizeof(uint32_t) * 7);

//
// Host Bus Adaptor (HBA)
//

typedef volatile struct packed {
  uint64_t cmd_list_base;   // 0x00-0x04 command list base address (1K-byte aligned)
  uint64_t fis_base;        // 0x08-0x0C FIS base address (256-byte aligned)
  uint32_t int_status;      // 0x10 interrupt status
  uint32_t int_enable;      // 0x14 interrupt enable
  uint32_t command;         // 0x18 command and status
  uint32_t : 32;		    // 0x1C reserved
  uint32_t task_file_data;  // 0x20 task file data
  uint32_t signature;       // 0x24 signature
  uint32_t sata_status;     // 0x28 SATA status (SCR0:SStatus)
  uint32_t sata_control;    // 0x2C SATA control (SCR2:SControl)
  uint32_t sata_error;      // 0x30 SATA error (SCR1:SError)
  uint32_t sata_active;     // 0x34 SATA active (SCR3:SActive)
  uint32_t command_issue;   // 0x38 command issue
  uint32_t sata_notif;      // 0x3C SATA notification (SCR4:SNotification)
  uint32_t fis_switch_ctrl; // 0x40 FIS-based switch control
  uint32_t reserved[11];    // 0x44-0x6F reserved
  uint32_t vendor[4];       // 0x70-0x7F vendor specific
} hba_port_t;

typedef union packed {
  uint32_t raw;
  struct {
    uint32_t num_ports : 5;       // number of supported ports
    uint32_t sxs_supported : 1;   // supports external SATA
    uint32_t ems_supported : 1;   // enclosure management supported
    uint32_t ccc_supported : 1;   // command completion coalescing supported
    uint32_t num_cmd_slots : 5;   // number of command slots
    uint32_t prtl_state_cap : 1;  // partial state capable
    uint32_t slmb_state_cap : 1;  // slumber state capable
    uint32_t pmd_supported : 1;   // PIO multiple DRQ block supported
    uint32_t fsb_supported : 1;   // FIS based switching supported
    uint32_t spm_supported : 1;   // port multiplier supported
    uint32_t ahci_mode_only : 1;  // supports AHCI mode only
    uint32_t : 1;                 // reserved
    uint32_t interface_speed : 4; // interface speed support
    uint32_t clo_supported : 1;   // command list override supported
    uint32_t aled_supported : 1;  // activity LED supported
    uint32_t alpm_supported : 1;  // aggressive link power management supported
    uint32_t ssu_supported : 1;   // staggered spin-up supported
    uint32_t mps_supported : 1;   // mechanical presence switch supported
    uint32_t sntf_supported : 1;  // SNotification register supported
    uint32_t ncq_supported : 1;   // native command queuing supported
    uint32_t a64_supported : 1;   // 64-bit addressing supported
  };
} hba_host_cap_t;
static_assert(sizeof(hba_host_cap_t) == sizeof(uint32_t));

typedef union packed {
  uint32_t raw;
  struct {
    uint32_t bos_supported : 1;  // BIOS/OS handoff mechanism supported
    uint32_t nvme_supported : 1; // NVMHCI present (nvme supported)
    uint32_t apst_supported : 1; // automatic partial to slumber transitions supported
    uint32_t ds_supported : 1;   // device sleep supported
    uint32_t adm_supported : 1;  // aggressive device sleep management supported
    uint32_t deso : 1;           // devsleep entrance from slumber only
    uint32_t : 26;               // reserved
  };
} hba_host_cap_ext_t;
static_assert(sizeof(hba_host_cap_ext_t) == sizeof(uint32_t));

typedef volatile struct packed {
  // 0x00 - 0x2B generic host control
  hba_host_cap_t host_cap;         // 0x00 host capability
  uint32_t global_host_ctrl;       // 0x04 global host control
  uint32_t int_status;             // 0x08 interrupt status
  uint32_t port_implemented;       // 0x0C port implemented
  uint32_t version;                // 0x10 version
  uint32_t cmd_cmpl_ctrl;          // 0x14 command completion coalescing control
  uint32_t cmd_cmpl_ports;         // 0x18 command completion coalescing ports
  uint32_t em_loc;                 // 0x1C enclosure management location
  uint32_t em_ctrl;                // 0x20 enclosure management control
  hba_host_cap_ext_t host_cap_ext; // 0x24 host capabilities extended
  uint32_t bios_ctrl_status;       // 0x28 BIOS/OS handoff control and status

  // 0x2C - 0x9F reserved
  uint8_t reserved[116];

  // 0xA0 - 0xFF vendor specific registers
  uint8_t vendor[96];

  // 0x100 - 0x10FF port control registers
  hba_port_t ports[1]; // 1-32
} hba_reg_mem_t;

typedef struct packed {
  // 0x00
  fis_dma_setup_t dma_setup_fis; // DMA setup FIS
  uint8_t padding1[4];           // padding
  // 0x20
  fis_pio_setup_t pio_setup_fis; // PIO setup FIS
  uint8_t padding2[12];          // padding
  // 0x40
  fis_reg_d2h_t d2h_fis;         // device to host FIS
  uint8_t padding3[4];           // padding
  // 0x58
  uint64_t sdb_fis;              // set device bits FIS
  // 0x60
  uint8_t unknown_fis[64];       // unknown FIS (???)
  // 0xA0
  uint8_t reserved[96];          // reserved
} hba_fis_t;
static_assert(sizeof(hba_fis_t) == 256);

// command header
typedef struct packed {
  // dword0
  uint32_t fis_length : 5;   // command FIS length in dwords (2 - 16)
  uint32_t atapi : 1;        // ATAPI
  uint32_t write : 1;        // write direction, 0 = D2H, 1 = H2D
  uint32_t prefetch : 1;     // prefetchable

  uint32_t reset : 1;        // reset
  uint32_t bist : 1;         // BIST
  uint32_t clear_bsy_ok : 1; // clear busy on R_OK
  uint32_t : 1;              // reserved
  uint32_t pmport : 4;       // port multiplier port

  uint32_t prdt_length : 16; // physical region descriptor table (PRDT) length

  // dword1
  volatile
  uint32_t prdb_transf_cnt;  // physical region descriptor byte count transferred

  // dword 2 & 3
  uint64_t cmd_table_base;   // command table descriptor table base address

  // dword4 - 7
  uint32_t reserved[4];      // reserved
} hba_cmd_header_t;
static_assert(sizeof(hba_cmd_header_t) == sizeof(uint32_t) * 8);

// PRDT entry
typedef struct packed {
  uint64_t data_base;       // data base address
  uint32_t : 32;            // reserved
  uint32_t byte_count : 22; // byte count, 4M max
  uint32_t : 9;             // reserved
  uint32_t ioc : 1;         // interrupt on completion
} hba_prdt_entry_t;

// HBA command table
typedef struct packed {
  // 0x00
  uint8_t cmd_fis[64];      // command FIS
  // 0x40
  uint8_t atapi_cmd[16];    // ATAPI command, 12 or 16 bytes
  // 0x50
  uint8_t reserved[48];     // reserved
  // 0x80
  hba_prdt_entry_t prdt[1]; // PRDT 0 - 65535
} hba_cmd_table_t;

//

typedef struct ahci_controller ahci_controller_t;


typedef struct ahci_slot {
  int num;
  hba_cmd_header_t *header;
  hba_cmd_table_t *table;
  size_t table_length;
} ahci_slot_t;

typedef struct ahci_device {
  int num;
  int type;
  hba_port_t *port;
  ahci_slot_t **slots;
  hba_fis_t *fis;
  ahci_controller_t *controller;
} ahci_device_t;

typedef struct ahci_controller {
  hba_reg_mem_t *mem;
  ahci_device_t **ports;
  pcie_device_t *pci;
} ahci_controller_t;

void ahci_init();
ssize_t ahci_read(fs_device_t *device, uint64_t lba, uint32_t count, void **buf);
ssize_t ahci_write(fs_device_t *device, uint64_t lba, uint32_t count, void **buf);
int ahci_release(fs_device_t *device, void *buf);

#endif
