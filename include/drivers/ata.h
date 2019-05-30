//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef DRIVERS_ATA_H
#define DRIVERS_ATA_H

#include <stdint.h>
#include <stddef.h>

// ATA Data Direction
#define ATA_READ      0x00
#define ATA_WRITE     0x01

// ATA Bus Channels
#define ATA_PRIMARY   0x00
#define ATA_SECONDARY 0x01

// ATA Drive Types
#define ATA_MASTER    0x00
#define ATA_SLAVE     0x01

// ATA I/O Ports
#define ATA_IO_PRIMARY     0x1F0
#define ATA_IO_SECONDARY   0x170
#define ATA_CTRL_PRIMARY   0x3F6
#define ATA_CTRL_SECONDARY 0x376

// ATA Registers
#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA_LO      3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HI      5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

#define ATA_REG_STATUS_ALT  0
#define ATA_REG_DEVICE_CTRL 0
#define ATA_REG_DEVICE_ADDR 1

// ATA Commands
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_FLUSH_CACHE   0xE7
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30


// ATA Error Register
enum ATAErrors {
  ERROR_AMNF,  // Address mark not found.
  ERROR_TKZNF, // Track zero not found.
  ERROR_ABRT,  // Aborted command.
  ERROR_MCR,   // Media change request.
  ERROR_IDNF,  // ID not found.
  ERROR_MC,    // Media changed.
  ERROR_UNC,   // Uncorrectable data error.
  ERROR_BBK,   // Bad Block detected.
};

// ATA Status Register
enum ATAStatus {
  STATUS_ERR,  // An error occurred.
  STATUS_IDX,  // Index.
  STATUS_CORR, // Corrected data.
  STATUS_DRQ,  // Ready to transfer/accept data.
  STATUS_SRV,  // Overlapped Mode Service Request.
  STATUS_DF,   // Drive Fault Error (does not set ERR).
  STATUS_RDY,  // Drive is spinning and no error.
  STATUS_BSY,  // Preparing to send/receive data.
};

// ATA Device Control Register
enum ATADeviceControl {
  DC0,  // Always set to zero.
  nIEN, // Stop ATA interrupts.
  SRST, // Software reset ATA drives.
  DC1,  // Reserved.
  DC2,  // Reserved.
  DC3,  // Reserved.
  DC4,  // Reserved.
  HOB,  // Read last HOB of last LBA48 value.
};

// ATA Device Address Register
enum ATADeviceAddress {
  DS0, // Drive 0 select.
  DS1, // Drive 1 select.
  HS0, // Representation of selected head.
  HS1, // Representation of selected head.
  HS2, // Representation of selected head.
  HS3, // Representation of selected head.
  WTG, // Write gate.
  DA0  // Reserved.
};


typedef struct {
  uint8_t type;
  size_t port_io;
  size_t port_data;
} ata_t;

typedef struct {
  uint8_t ata_device   : 1; // word 0 [bit  0]     | 0 = ATA device
  uint8_t lba_enabled  : 1; // word 49 [bit 9]     | 1 = LBA supported
  uint8_t dma_enabled  : 1; // word 49 [bit 8]     | 1 = DMA supported
  uint8_t dma_modes    : 4; // word 63 [bits 0-2]  | Available DMA modes
  uint8_t dma_selected : 4; // word 63 [bits 8-10] | Selected DMA modes
  uint32_t sectors;         // word 60 & 61        | Number of sectors
  // word 88 (Ultra DMA modes)
  // word 93 (Hardware reset result)
} ata_info_t;


void init_ata();
void ata_identify(ata_t *disk, ata_info_t *info);
void ata_read(ata_t *disk, size_t lba, int sectors, uint8_t *buffer);
void ata_write(ata_t *disk, size_t lba, int sectors, uint8_t *buffer);
void ata_read_sector(ata_t *disk, size_t lba, uint8_t *buffer);
void ata_write_sector(ata_t *disk, size_t lba, uint8_t *buffer);

#endif //DRIVERS_ATA_H
