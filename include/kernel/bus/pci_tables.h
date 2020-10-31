//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef KERNEL_BUS_PCI_TABLES_H
#define KERNEL_BUS_PCI_TABLES_H

#include <base.h>

/* --------------- PCI Classses --------------- */


const char *pci_class_descriptions[] = {
    "Unclassified",
    "Mass Storage Controller",
    "Network Controller",
    "Display Controller",
    "Multimedia Controller",
    "Memory Controller",
    "Bridge Device",
    "Simple Communication Controller",
    "Base System Peripheral",
    "Input Device Controller",
    "Docking Station",
    "Processor",
    "Serial Bus Controller",
    "Wireless Controller",
    "Intelligent Controller",
    "Satellite Communication Controller",
    "Encryption Controller",
    "Signal Processing Controller",
    "Processing Accelerator",
    "Non-Essential Instrumentation",
    "Reserved",
    "Unassigned Class (Vendor specific)",
};

static inline const char *pci_get_class_desc(uint8_t class_code) {
  if (class_code >= 0x14 && class_code <= 0x3F) return pci_class_descriptions[19];
  if (class_code >= 0x41 && class_code <= 0xFE) return pci_class_descriptions[19];
  return pci_class_descriptions[class_code];
}


/* --------------- PCI Subclassses --------------- */

typedef struct pci_subclass {
  uint8_t class_code;
  uint8_t subclass_code;
  const char *desc;
} pci_subclass_t;

const pci_subclass_t pci_subclass_descriptions[] = {
    { 0x00, 0x00, "Non-VGA-Compatible Device" },
    { 0x00, 0x01, "VGA-Compatible Device"},

    { 0x01, 0x00, "SCSI Bus Controller" },
    { 0x01, 0x01, "IDE Controller" },
    { 0x01, 0x02, "Floppy Disk Controller" },
    { 0x01, 0x03, "IPI Bus Controller" },
    { 0x01, 0x04, "RAID Controller" },
    { 0x01, 0x05, "ATA Controller" },
    { 0x01, 0x06, "Serial ATA" },
    { 0x01, 0x07, "Serial Attached SCSI" },
    { 0x01, 0x08, "Non-Volatile Memory Controller" },
    { 0x01, 0x80, "Other" },

    { 0x02, 0x00, "Ethernet Controller" },
    { 0x02, 0x01, "Token Ring Controller" },
    { 0x02, 0x02, "FDDI Controller" },
    { 0x02, 0x03, "ATM Controller" },
    { 0x02, 0x04, "ISDN Controller" },
    { 0x02, 0x05, "WorldFip Controller" },
    { 0x02, 0x06, "PICMG 2.14 Multi Computing" },
    { 0x02, 0x07, "Infiniband Controller" },
    { 0x02, 0x08, "Fabric Controller" },
    { 0x02, 0x80, "Other" },

    { 0x03, 0x00, "VGA Compatible Controller" },
    { 0x03, 0x01, "XGA Controller" },
    { 0x03, 0x02, "3D Controller (Not VGA-Compatible)" },
    { 0x03, 0x80, "Other" },

    { 0x04, 0x00, "Multimedia Video Controller" },
    { 0x04, 0x01, "Multimedia Audio Controller" },
    { 0x04, 0x02, "Computer Telephony Device" },
    { 0x04, 0x03, "Audio Device" },
    { 0x04, 0x80, "Other" },

    { 0x05, 0x00, "RAM Controller" },
    { 0x05, 0x01, "Flash Controller" },
    { 0x05, 0x80, "Other" },

    { 0x06, 0x00, "Host Bridge" },
    { 0x06, 0x01, "ISA Bridge" },
    { 0x06, 0x02, "EISA Bridge" },
    { 0x06, 0x03, "MCA Bridge" },
    { 0x06, 0x04, "PCI-to-PCI Bridge" },
    { 0x06, 0x05, "PCMCIA Bridge" },
    { 0x06, 0x06, "NuBus Bridge" },
    { 0x06, 0x07, "CardBus Bridge" },
    { 0x06, 0x08, "RACEway Bridge" },
    { 0x06, 0x09, "PCI-to-PCI Bridge" },
    { 0x06, 0x0A, "InfiniBand-to-PCI Host Bridge" },
    { 0x06, 0x80, "Other" },

    { 0x07, 0x00, "Serial Controller" },
    { 0x07, 0x01, "Parallel Controller" },
    { 0x07, 0x02, "Multiport Serial Controller" },
    { 0x07, 0x03, "Modem" },
    { 0x07, 0x04, "IEEE 488.1/2 (GPIB) Controller" },
    { 0x07, 0x05, "Smart Card" },
    { 0x07, 0x80, "Other" },

    { 0x08, 0x00, "PIC" },
    { 0x08, 0x01, "DMA Controller" },
    { 0x08, 0x02, "Timer" },
    { 0x08, 0x03, "RTC Controller" },
    { 0x08, 0x04, "PCI Hot-Plug Controller" },
    { 0x08, 0x05, "SD Host controller" },
    { 0x08, 0x06, "IOMMU" },
    { 0x08, 0x80, "Other" },

    { 0x09, 0x00, "Keyboard Controller" },
    { 0x09, 0x01, "Digitizer Pen" },
    { 0x09, 0x02, "Mouse Controller" },
    { 0x09, 0x03, "Scanner Controller" },
    { 0x09, 0x04, "Gameport Controller" },
    { 0x09, 0x80, "Other" },

    { 0x0A, 0x00, "Generic" },
    { 0x0A, 0x80, "Other" },

    { 0x0B, 0x00, "386" },
    { 0x0B, 0x01, "486" },
    { 0x0B, 0x02, "Pentium" },
    { 0x0B, 0x03, "Pentium Pro" },
    { 0x0B, 0x10, "Alpha" },
    { 0x0B, 0x20, "PowerPC" },
    { 0x0B, 0x30, "MIPS" },
    { 0x0B, 0x40, "Co-Processor" },
    { 0x0B, 0x80, "Other" },

    { 0x0C, 0x00, "FireWire (IEEE 1394) Controller" },
    { 0x0C, 0x01, "ACCESS Bus" },
    { 0x0C, 0x02, "SSA" },
    { 0x0C, 0x03, "USB Controller" },
    { 0x0C, 0x04, "Fibre Channel" },
    { 0x0C, 0x05, "SMBus" },
    { 0x0C, 0x06, "InfiniBand" },
    { 0x0C, 0x07, "IPMI Interface" },
    { 0x0C, 0x08, "SERCOS Interface (IEC 61491)" },
    { 0x0C, 0x09, "CANbus" },
    { 0x0C, 0x80, "Other" },

    { 0x0D, 0x00, "iRDA Compatible Controller" },
    { 0x0D, 0x01, "Consumer IR Controller" },
    { 0x0D, 0x10, "RF Controller" },
    { 0x0D, 0x11, "Bluetooth Controller" },
    { 0x0D, 0x12, "Broadband Controller" },
    { 0x0D, 0x20, "Ethernet Controller (802.1a)" },
    { 0x0D, 0x21, "Ethernet Controller (802.1b)" },
    { 0x0D, 0x80, "Other" },

    { 0x0E, 0x00, "I20" },

    { 0x0F, 0x01, "Satellite TV Controller" },
    { 0x0F, 0x02, "Satellite Audio Controller" },
    { 0x0F, 0x03, "Satellite Voice Controller" },
    { 0x0F, 0x04, "Satellite Data Controller" },

    { 0x10, 0x00, "Network and Computing Encrpytion/Decryption" },
    { 0x10, 0x10, "Entertainment Encryption/Decryption" },
    { 0x10, 0x80, "Other Encryption/Decryption" },

    { 0x11, 0x00, "DPIO Modules" },
    { 0x11, 0x01, "Performance Counters" },
    { 0x11, 0x10, "Communication Synchronizer" },
    { 0x11, 0x20, "Signal Processing Management" },
    { 0x11, 0x80, "Other" },
};



static inline const char *pci_get_subclass_desc(uint8_t class_code, uint8_t subclass_code) {
  if (class_code >= 0x14 && class_code <= 0x3F) return "";
  if (class_code >= 0x41 && class_code <= 0xFE) return "";

  int max_index = sizeof(pci_subclass_descriptions) / sizeof(pci_subclass_t);
  for (int i = 0; i < max_index; i++) {
    pci_subclass_t subclass = pci_subclass_descriptions[i];
    if (subclass.class_code == class_code && subclass.subclass_code == subclass_code) {
      return subclass.desc;
    }
  }
  return "";
}



#endif
