//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef KERNEL_BUS_PCI_H
#define KERNEL_BUS_PCI_H

#include <base.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

// Device Classes
#define PCI_STORAGE_CONTROLLER    0x01
#define PCI_NETWORK_CONTROLLER    0x02
#define PCI_DISPLAY_CONTROLLER    0x03
#define PCI_BRIDGE_DEVICE         0x06
#define PCI_BASE_PERIPHERAL       0x08
#define PCI_SERIAL_BUS_CONTROLLER 0x0C

// Mass Storage Controllers
#define PCI_SCSI_BUS_CONTROLLER 0x00
#define PCI_IDE_CONTROLLER 0x01
#define PCI_FLOPPY_DISK_CONTROLLER 0x02
#define PCI_ATA_CONTROLLER 0x05
#define PCI_SERIAL_ATA_CONTROLLER 0x06

// Network Controllers
#define PCI_ETHERNET_CONTROLLER 0x00

// Display Controllers
#define PCI_VGA_CONTROLLER 0x00

// Bridge Devices
#define PCI_HOST_BRIDGE 0x00
#define PCI_ISA_BRIDGE 0x01
#define PCI_PCI_BRIDGE 0x04

// Serial Bus Controllers
#define PCI_USB_CONTROLLER 0x03

#define USB_PROG_IF_UHCI 0x00
#define USB_PROG_IF_OHCI 0x10
#define USB_PROG_IF_EHCI 0x20 // USB2
#define USB_PROG_IF_XHCI 0x30 // USB3


/* --------------- PCI Registers --------------- */


/* Status Register */

typedef union {
  uint16_t raw;
  struct {
    uint16_t reserved0 : 2;        // reserved
    uint16_t int_status : 1;       // interrupt status
    uint16_t cap_list : 1;         // capabilities list at 0x34
    uint16_t dev_freq : 1;         // 66 MHz capable
    uint16_t reserved1 : 1;        // reserved
    uint16_t fast_b2b : 1;         // fast back-to-back capable
    uint16_t master_parity : 1;    // master data parity error
    uint16_t devsel_timing : 2;    // DEVSEL timing
    uint16_t sig_target_abrt : 1;  // signalled target abort
    uint16_t recv_target_abrt : 1; // received target abort
    uint16_t recv_master_abrt : 1; // recevied master abort
    uint16_t sig_system_err : 1;   // signalled system error
    uint16_t det_parity_err : 1;   // detected parity error
  };
} pci_status_t;


/* Command Register */

typedef union {
  uint16_t raw;
  struct {
    uint16_t io_space : 1;          // I/O space
    uint16_t mem_space : 1;         // memory space
    uint16_t bus_master : 1;        // bus master
    uint16_t special_cycles : 1;    // special cycles
    uint16_t mem_write_invld : 1;   // memory write and invalidate
    uint16_t vga_palette_snoop : 1; // vga palette snoop
    uint16_t parity_err_resp : 1;   // parity error response
    uint16_t reserved0 : 1;         // reserved
    uint16_t serr_enable : 1;       // SERR# enable
    uint16_t fast_b2b_enable : 1;   // fast back-to-back enable
    uint16_t int_disable : 1;       // interrupt disable
    uint16_t reserved1 : 5;         // reserved
  };
} pci_command_t;


/* Header Type Register */

#define PCI_HEADER_STANDARD 0x00
#define PCI_HEADER_BRIDGE   0x01
#define PCI_HEADER_CARDBUS  0x02

typedef union {
  uint8_t raw;
  struct {
    uint8_t type : 7;    // header type
    uint8_t multi_function : 1; // multiple functions
  };
} pci_header_type_t;


/* BIST Reigster */

typedef union {
  uint8_t raw;
  struct {
    uint8_t compl_code : 4;   // completion code
    uint8_t reserved : 2;     // reserved
    uint8_t start_bist : 1;   // start BIST
    uint8_t bist_capable : 1; // BIST capable
  };
} pci_bist_t;

/* --------------- PCI Structures --------------- */

typedef struct {
  uint32_t bar_type : 1;  // bar type - 0 = memory, 1 = i/o
  uint32_t addr_type : 2; // address type (memory only)
  uint32_t prefetch : 1;  // prefetchable (memory only)
  uint32_t : 28;          // reserved
  uint64_t base_addr;     // base address
  uint64_t size;          // size
} pci_bar_t;

typedef struct pci_device {
  uint8_t bus;                // bus number
  uint8_t device : 5;         // device number
  uint8_t function : 3;       // device function
  uint16_t vendor_id;         // vendor id
  uint16_t device_id;         // device id
  uint8_t class_code;         // device class
  uint8_t subclass_code;      // device subclass
  uint8_t prog_if;            // device programming interface
  uint8_t header_type : 7;    // device header type
  uint8_t multi_function : 1; // multi-function device
  uint8_t interrupt_line;     // interrupt line
  uint8_t interrupt_pin;      // interrupt pin
  uint8_t bar_count;          // number of base address registers
  pci_bar_t *bars;            // array of pci_bar_t structures
  struct pci_device *next;    // pointer to next device function
} pci_device_t;


#define PCI_PROBE_CONTINUE 0
#define PCI_PROBE_STOP 1

typedef int (*pci_callback_t)(pci_device_t*, void *context);

/* --------------- PCI Headers --------------- */

/*
 *  Header Type 0x00 (Standard Header)
 *
 *  register	offset	bits 31-24	bits 23-16	bits 15-8	bits 7-0
 *  00	        00	Device ID	                Vendor ID
 *  01	        04	Status	                        Command
 *  02	        08	Class code	Subclass	Prog IF	        Revision ID
 *  03	        0C	BIST	        Header type	Latency Timer	Cache Line Size
 *  04	        10	Base address #0 (BAR0)
 *  05	        14	Base address #1 (BAR1)
 *  06	        18	Base address #2 (BAR2)
 *  07	        1C	Base address #3 (BAR3)
 *  08	        20	Base address #4 (BAR4)
 *  09	        24	Base address #5 (BAR5)
 *  0A	        28	Cardbus CIS Pointer
 *  0B	        2C	Subsystem ID	                Subsystem Vendor ID
 *  0C	        30	Expansion ROM base address
 *  0D	        34	Reserved	Capabilities Pointer
 *  0E	        38	Reserved
 *  0F	        3C	Max latency	Min Grant	Interrupt PIN	Interrupt Line
 */

typedef struct {

} pci_standard_header_t;


/*
 * Header Type 0x01 (PCI-to-PCI bridge)
 *
 * register	offset	bits 31-24	bits 23-16	bits 15-8	bits 7-0
 * 00	        00	Device ID	                Vendor ID
 * 01	        04	Status	                        Command
 * 02	        08	Class code	Subclass	Prog IF	        Revision ID
 * 03	        0C	BIST	        Header type	Latency Timer	Cache Line Size
 * 04	        10	Base address #0 (BAR0)
 * 05	        14	Base address #1 (BAR1)
 * 06	        18	Sec.Lat. Timer	Subord.Bus Num	Sec. Bus Num	Prim. Bus Num
 * 07	        1C	Secondary Status	        I/O Limit	I/O Base
 * 08	        20	Memory Limit	                Memory Base
 * 09	        24	Prefetchable Memory Limit	Prefetchable Memory Base
 * 0A	        28	Prefetchable Base Upper 32 Bits
 * 0B	        2C	Prefetchable Limit Upper 32 Bits
 * 0C	        30	I/O Limit Upper 16 Bits	        I/O Base Upper 16 Bits
 * 0D	        34	Reserved	                                Capability Pointer
 * 0E	        38	Expansion ROM base address
 * 0F	        3C	Bridge Control	                Interrupt PIN	Interrupt Line
 */

typedef struct {

} pci_bridge_header_t;


/* --------------- Public Functions --------------- */

uint32_t pci_read(uint32_t addr, uint8_t offset);
void pci_write(uint32_t addr, uint8_t offset, uint32_t value);

int pci_probe_device(pci_device_t *device, pci_callback_t callback, void *context);
int pci_probe_bus(uint8_t bus, pci_callback_t callback, void *context);
void pci_probe_busses(pci_callback_t callback, void *context);
void pci_enumerate_busses();
pci_device_t *pci_locate_device(uint8_t device_class, uint8_t device_subclass, int prog_if);

void pci_print_debug_device(pci_device_t *device);

#endif
