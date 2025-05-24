//
// Created by Aaron Gill-Braun on 2021-03-08.
//

#include <kernel/bus/pci_tables.h>

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

/* --------------- PCI Subclassses --------------- */

const pci_desc_t pci_subclass_descriptions[] = {
  // 0x00 - Unclassified Devices
  { 0x00, 0x00, 0x00, "Non-VGA-Compatible Device" },
  { 0x00, 0x01, 0x00, "VGA-Compatible Device" },

  // 0x01 - Mass Storage Controllers
  { 0x01, 0x00, 0x00, "SCSI Bus Controller" },
  // IDE Controller
  { 0x01, 0x01, 0x00, "IDE Controller - ISA Compatibility mode-only controller" },
  { 0x01, 0x01, 0x05, "IDE Controller - PCI native mode-only controller" },
  { 0x01, 0x01, 0x0A, "IDE Controller - ISA Compatibility mode controller, supports both channels switched to PCI native mode" },
  { 0x01, 0x01, 0x0F, "IDE Controller - PCI native mode controller, supports both channels switched to ISA compatibility mode" },
  { 0x01, 0x01, 0x80, "IDE Controller - ISA Compatibility mode-only controller, supports bus mastering" },
  { 0x01, 0x01, 0x85, "IDE Controller - PCI native mode-only controller, supports bus mastering" },
  { 0x01, 0x01, 0x8A, "IDE Controller - ISA Compatibility mode controller, supports both channels switched to PCI native mode, supports bus mastering" },
  { 0x01, 0x01, 0x8F, "IDE Controller - PCI native mode controller, supports both channels switched to ISA compatibility mode, supports bus mastering" },

  { 0x01, 0x02, 0x00, "Floppy Disk Controller" },
  { 0x01, 0x03, 0x00, "IPI Bus Controller" },
  { 0x01, 0x04, 0x00, "RAID Controller" },
  // ATA Controller
  { 0x01, 0x05, 0x20, "ATA Controller - Single DMA" },
  { 0x01, 0x05, 0x30, "ATA Controller - Chained DMA" },
  // SATA Controller
  { 0x01, 0x06, 0x00, "SATA Controller - Vendor Specific Interface" },
  { 0x01, 0x06, 0x01, "SATA Controller - AHCI 1.0" },
  { 0x01, 0x06, 0x02, "SATA Controller - Serial Storage Bus" },

  { 0x01, 0x07, 0x00, "Serial Attached SCSI Controller" },
  { 0x01, 0x08, 0x00, "Non-Volatile Memory Controller" },

  // 0x02 - Network Controllers
  { 0x02, 0x00, 0x00, "Ethernet Controller" },
  { 0x02, 0x01, 0x00, "Token Ring Controller" },
  { 0x02, 0x02, 0x00, "FDDI Controller" },
  { 0x02, 0x03, 0x00, "ATM Controller" },
  { 0x02, 0x04, 0x00, "ISDN Controller" },
  { 0x02, 0x05, 0x00, "WorldFip Controller" },
  { 0x02, 0x06, 0x00, "PICMG 2.14 Multi Computing" },
  { 0x02, 0x07, 0x00, "InfiniBand Controller" },
  { 0x02, 0x08, 0x00, "Fabric Controller" },

  // 0x03 - Display Controllers
  { 0x03, 0x00, 0x00, "VGA Compatible Controller" },
  { 0x03, 0x00, 0x01, "8514-Compatible Controller" },
  { 0x03, 0x01, 0x00, "XGA Controller" },
  { 0x03, 0x02, 0x00, "3D Controller" },

  // 0x04 - Multimedia Controllers
  { 0x04, 0x00, 0x00, "Multimedia Video Controller" },
  { 0x04, 0x01, 0x00, "Multimedia Audio Controller" },
  { 0x04, 0x02, 0x00, "Computer Telephony Device" },
  { 0x04, 0x03, 0x00, "Audio Device" },

  // 0x05 - Memory Controllers
  { 0x05, 0x00, 0x00, "RAM Controller" },
  { 0x05, 0x01, 0x00, "Flash Controller" },

  // 0x06 - Bridge Devices
  { 0x06, 0x00, 0x00, "Host Bridge" },
  { 0x06, 0x01, 0x00, "ISA Bridge" },
  { 0x06, 0x02, 0x00, "EISA Bridge" },
  { 0x06, 0x03, 0x00, "MCA Bridge" },
  { 0x06, 0x04, 0x00, "PCI-to-PCI Bridge" },
  { 0x06, 0x04, 0x01, "PCI-to-PCI Bridge (Subtractive Decode)" },
  { 0x06, 0x05, 0x00, "PCMCIA Bridge" },
  { 0x06, 0x06, 0x00, "NuBus Bridge" },
  { 0x06, 0x07, 0x00, "CardBus Bridge" },
  { 0x06, 0x08, 0x00, "RACEway Bridge" },
  { 0x06, 0x09, 0x00, "PCI-to-PCI Bridge" },
  { 0x06, 0x0A, 0x00, "InfiniBand-to-PCI Host Bridge" },

  // 0x07 - Simple Communication Controllers
  { 0x07, 0x00, 0x00, "Serial Controller - 8250-Compatible" },
  { 0x07, 0x00, 0x01, "Serial Controller - 16450-Compatible" },
  { 0x07, 0x00, 0x02, "Serial Controller - 16550-Compatible" },
  { 0x07, 0x00, 0x03, "Serial Controller - 16650-Compatible" },
  { 0x07, 0x00, 0x04, "Serial Controller - 16750-Compatible" },
  { 0x07, 0x00, 0x05, "Serial Controller - 16850-Compatible" },
  { 0x07, 0x00, 0x06, "Serial Controller - 16950-Compatible" },

  { 0x07, 0x01, 0x00, "Parallel Controller" },
  { 0x07, 0x01, 0x01, "Parallel Controller - Bi-Directional" },
  { 0x07, 0x01, 0x02, "Parallel Controller - ECP 1.X Compliant" },
  { 0x07, 0x01, 0x03, "Parallel Controller - IEEE 1284 Controller" },
  { 0x07, 0x01, 0xFE, "Parallel Controller - IEEE 1284 Target Device" },

  { 0x07, 0x02, 0x00, "Multiport Serial Controller" },
  { 0x07, 0x03, 0x00, "Modem - Generic" },
  { 0x07, 0x03, 0x01, "Modem - Hayes 16450-Compatible" },
  { 0x07, 0x03, 0x02, "Modem - Hayes 16550-Compatible" },
  { 0x07, 0x03, 0x03, "Modem - Hayes 16650-Compatible" },
  { 0x07, 0x03, 0x04, "Modem - Hayes 16750-Compatible" },

  // 0x08 - Base System Peripherals
  { 0x08, 0x00, 0x00, "Generic 8259 PIC" },
  { 0x08, 0x00, 0x01, "ISA PIC" },
  { 0x08, 0x00, 0x02, "EISA PIC" },
  { 0x08, 0x00, 0x10, "IO-APIC" },
  { 0x08, 0x00, 0x20, "IO(X)-APIC" },

  { 0x08, 0x01, 0x00, "Generic 8237 DMA Controller" },
  { 0x08, 0x01, 0x01, "ISA DMA Controller" },
  { 0x08, 0x01, 0x02, "EISA DMA Controller" },

  { 0x08, 0x02, 0x00, "Generic 8254 System Timer" },
  { 0x08, 0x02, 0x01, "ISA System Timer" },
  { 0x08, 0x02, 0x02, "EISA System Timer" },

  { 0x08, 0x03, 0x00, "Generic RTC Controller" },
  { 0x08, 0x03, 0x01, "ISA RTC Controller" },

  { 0x08, 0x04, 0x00, "Generic PCI Hot-Plug Controller" },
  { 0x08, 0x05, 0x00, "SD Host Controller" },
  { 0x08, 0x06, 0x00, "IOMMU" },

  // 0x09 - Input Devices
  { 0x09, 0x00, 0x00, "Keyboard Controller" },
  { 0x09, 0x01, 0x00, "Digitizer Pen" },
  { 0x09, 0x02, 0x00, "Mouse Controller" },
  { 0x09, 0x03, 0x00, "Scanner Controller" },
  { 0x09, 0x04, 0x00, "Gameport Controller" },
  { 0x09, 0x04, 0x10, "Gameport Controller - Generic" },

  // 0x0A - Docking Stations
  { 0x0A, 0x00, 0x00, "Generic Docking Station" },

  // 0x0B - Processors
  { 0x0B, 0x00, 0x00, "386 Processor" },
  { 0x0B, 0x01, 0x00, "486 Processor" },
  { 0x0B, 0x02, 0x00, "Pentium Processor" },
  { 0x0B, 0x10, 0x00, "Alpha Processor" },
  { 0x0B, 0x20, 0x00, "PowerPC Processor" },
  { 0x0B, 0x30, 0x00, "MIPS Processor" },
  { 0x0B, 0x40, 0x00, "Co-Processor" },

  // 0x0C - Serial Bus Controllers
  { 0x0C, 0x00, 0x00, "IEEE 1394 Controller - FireWire" },
  { 0x0C, 0x00, 0x10, "IEEE 1394 Controller - OpenHCI" },

  { 0x0C, 0x03, 0x00, "USB UHCI Controller" },
  { 0x0C, 0x03, 0x10, "USB OHCI Controller" },
  { 0x0C, 0x03, 0x20, "USB 2.0 EHCI Controller" },
  { 0x0C, 0x03, 0x30, "USB 3.0 xHCI Controller" },
  { 0x0C, 0x03, 0x80, "USB Controller - No specific programming interface" },
  { 0x0C, 0x03, 0xFE, "USB Device (Not a host controller)" },

  { 0x0C, 0x04, 0x00, "Fibre Channel Controller" },
  { 0x0C, 0x05, 0x00, "SMBus Controller" },
  { 0x0C, 0x06, 0x00, "InfiniBand Controller" },
  { 0x0C, 0x07, 0x00, "IPMI Interface - SMIC" },
  { 0x0C, 0x07, 0x01, "IPMI Interface - Keyboard Controller Style" },
  { 0x0C, 0x07, 0x02, "IPMI Interface - Block Transfer" },

  { 0x0C, 0x08, 0x00, "SERCOS Interface" },
  { 0x0C, 0x09, 0x00, "CANbus Controller" },

  // 0x0D - Wireless Controllers
  { 0x0D, 0x00, 0x00, "iRDA Compatible Controller" },
  { 0x0D, 0x01, 0x00, "Consumer IR Controller" },
  { 0x0D, 0x10, 0x00, "RF Controller" },
  { 0x0D, 0x11, 0x00, "Bluetooth Controller" },
  { 0x0D, 0x12, 0x00, "Broadband Controller" },
  { 0x0D, 0x20, 0x00, "Ethernet Controller (802.1a)" },
  { 0x0D, 0x21, 0x00, "Ethernet Controller (802.1b)" },

  // 0x0E - Intelligent Controller
  { 0x0E, 0x00, 0x00, "I2O Controller" },

  // 0x0F - Satellite Communications Controllers
  { 0x0F, 0x00, 0x00, "Satellite TV Controller" },
  { 0x0F, 0x01, 0x00, "Satellite Audio Controller" },
  { 0x0F, 0x02, 0x00, "Satellite Voice Controller" },
  { 0x0F, 0x03, 0x00, "Satellite Data Controller" },

  // 0x10 - Encryption Controllers
  { 0x10, 0x00, 0x00, "Network and Computing Encryption Device" },
  { 0x10, 0x10, 0x00, "Entertainment Encryption Device" },

  // 0x11 - Signal Processing Controllers (continued)
  { 0x11, 0x00, 0x00, "DPIO Modules" },
  { 0x11, 0x01, 0x00, "Performance Counters" },
  { 0x11, 0x10, 0x00, "Communication Synchronizer" },
  { 0x11, 0x20, 0x00, "Signal Processing Management" },
  { 0x11, 0x21, 0x00, "Digital Signal Processor" },

  // Terminator
  { 0xFF, 0xFF, 0xFF, NULL }
};


const char *pci_get_device_desc(uint8_t class_code, uint8_t subclass_code, uint8_t prog_if) {
  for (int i = 0; i < ARRAY_SIZE(pci_subclass_descriptions); i++) {
    if (pci_subclass_descriptions[i].class_code == class_code &&
        pci_subclass_descriptions[i].subclass_code == subclass_code &&
        pci_subclass_descriptions[i].prog_if == prog_if) {
      return pci_subclass_descriptions[i].desc;
    }
  }

  if (class_code < ARRAY_SIZE(pci_class_descriptions)) {
    return pci_class_descriptions[class_code];
  }
  return "Unknown Device";
}
