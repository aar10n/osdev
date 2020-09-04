//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <drivers/ata_pio.h>
#include <kernel/cpu/asm.h>

/* Delay */
void ata_delay(ata_t *disk) {
  for (int i = 0; i < 4; i++) {
    inb(disk->port_data + ATA_REG_STATUS_ALT);
  }
}

/* Poll disk status */
int ata_poll(ata_t *disk) {
  ata_delay(disk);

  uint8_t status;
  while (true) {
    status = inb(disk->port_io + ATA_REG_STATUS);
    if (!(status & (1 << STATUS_RDY))) {
      kprintf("No drive found\n");
      return 1;
    }

    if (!(status & (1 << STATUS_BSY))) {
      while (true) {
        status = inb(disk->port_io + ATA_REG_STATUS);

        if (status & (1 << STATUS_ERR)) {
          kprintf("error: %b\n", inb(disk->port_data + ATA_REG_ERROR));
          return 1;
        }

        if (status & (1 << STATUS_DRQ)) return 0;
      }
    }
  }
}

/* Select drive */
void ata_drive_select(ata_t *disk) {
  outb(disk->port_io + ATA_REG_DRIVE, disk->type);
  ata_delay(disk);
}

/* Perform a disk read/write */
void ata_read_write(bool operation, ata_t *disk, size_t lba, int sectors, uint8_t *buffer) {
  uint8_t drive_type = (disk->type == ATA_MASTER ? 0xE0 : 0xF0);
  uint8_t slave_bit = (disk->type == ATA_MASTER ? 0x00 : 0x01);

  outb(disk->port_io + ATA_REG_DRIVE, drive_type | (slave_bit << 4) | ((lba >> 24) & 0x0F));
  outb(disk->port_io + ATA_REG_ERROR, 0x00);
  outb(disk->port_io + ATA_REG_SECCOUNT, (uint8_t) sectors);
  outb(disk->port_io + ATA_REG_LBA_LO, (uint8_t) lba);
  outb(disk->port_io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
  outb(disk->port_io + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));

  if (operation == ATA_READ) {
    // Send the READ SECTORS command
    outb(disk->port_io + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
  } else {
    // Send the WRITE SECTORS command
    outb(disk->port_io + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
  }

  // For each sector
  for (int k = 0; k < sectors; k++) {
    int err = ata_poll(disk);
    if (err != 0) return;

    // Read or write data to disk
    for (int i = (512 * k); i < (512 * (k + 1)); i += 2) {
      if (operation == ATA_READ) {
        uint16_t data = inw(disk->port_io + ATA_REG_DATA);
        buffer[i] = (data & 0xFF);
        buffer[i + 1] = (data >> 8);
      } else {
        uint16_t data = (buffer[i + 1] << 8) | (buffer[i] & 0xFF);
        outw(disk->port_io + ATA_REG_DATA, data);
      }
    }

    if (operation == ATA_WRITE) {
      // Send the FLUSH CACHE command
      outb(disk->port_io + ATA_REG_COMMAND, ATA_CMD_FLUSH_CACHE);
    }
    ata_delay(disk);
  }
}

//
// Public Functions
//

/* Identify a disk */
void ata_identify(ata_t *disk, ata_info_t *info) {
  // Select the drive
  ata_drive_select(disk);

  outb(disk->port_io + ATA_REG_SECCOUNT, 0x00); // Set sector count to 0
  outb(disk->port_io + ATA_REG_LBA_LO, 0x00);   // Set LBAlo to 0
  outb(disk->port_io + ATA_REG_LBA_MID, 0x00);  // Set LBAmid to 0
  outb(disk->port_io + ATA_REG_LBA_HI, 0x00);   // Set LBAhi to 0

  // Send the IDENTIFY command
  outb(disk->port_io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

  int err = ata_poll(disk);
  if (err != 0) {
    info->ata_device = 0;
    return;
  }

  uint16_t buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = inw(disk->port_io + ATA_REG_DATA);
  }

  // Populate the info struct
  info->ata_device = ~(buffer[0] & 0x1);
  info->lba_enabled = (buffer[49] >> 9) & 0x1;
  info->dma_enabled = (buffer[49] >> 8) & 0x1;
  info->dma_modes = buffer[63] & 0xF;
  info->dma_selected = (buffer[63] >> 8) & 0xF;
  info->sectors = (buffer[61] << 16) | (buffer[60] & 0xFF);

  ata_delay(disk);
}

/* Read multiple sectors from disk */
void ata_read(ata_t *disk, size_t lba, int sectors, uint8_t *buffer) {
  ata_read_write(ATA_READ, disk, lba, sectors, buffer);
}

/* Write multiple sectors to disk */
void ata_write(ata_t *disk, size_t lba, int sectors, uint8_t *buffer) {
  ata_read_write(ATA_WRITE, disk, lba, sectors, buffer);
}

/* Read one sector from disk */
void ata_read_sector(ata_t *disk, size_t lba, uint8_t *buffer) {
  ata_read_write(ATA_READ, disk, lba, 1, buffer);
}

/* Write one sector to disk */
void ata_write_sector(ata_t *disk, size_t lba, uint8_t *buffer) {
  ata_read_write(ATA_WRITE, disk, lba, 1, buffer);
}

/* Initialize the ATA driver */
void init_ata() {
  ata_info_t info;

  ata_t primary_bus = ATA_DRIVE_PRIMARY;
  ata_identify(&primary_bus, &info);
  if (!info.ata_device) {
    ata_t secondary_bus = ATA_DRIVE_SECONDARY;
    ata_identify(&secondary_bus, &info);

    if (!info.ata_device) {
      kprintf("No drives found.\n");
    }
  }
}

// Debugging

void ata_print_debug_ata_disk(ata_t *disk) {
  kprintf("disk = {\n"
          "  type = %#X\n"
          "  port_io = %#X\n"
          "  port_data = %#X\n"
          "}\n",
          disk->type,
          disk->port_io,
          disk->port_data);
}

void ata_print_debug_ata_info(ata_info_t *info) {
  kprintf("info = {\n"
          "  ata_device = %#X\n"
          "  lba_enabled = %d\n"
          "  dma_enabled = %d\n"
          "  dma_modes = %d\n"
          "  dma_selected = %d\n"
          "  sectors = %u\n"
          "}\n",
          info->ata_device,
          info->lba_enabled,
          info->dma_enabled,
          info->dma_modes,
          info->dma_selected,
          info->sectors);
}
