//
// Created by Aaron Gill-Braun on 2020-09-01.
//

#include <drivers/ahci.h>
#include <drivers/ata_pio.h>
#include <string.h>

void ahci_identify() {
  FIS_REG_H2D fis;
  memset(&fis, 0, sizeof(FIS_REG_H2D));
  fis.fis_type = FIS_TYPE_REG_H2D;
  fis.command = ATA_CMD_IDENTIFY;	// 0xEC
  fis.device = 0;			// Master device
  fis.c = 1;				// Write command register
}
