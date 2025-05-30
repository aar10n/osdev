//
// Created by Aaron Gill-Braun on 2023-02-05.
//

#ifndef KERNEL_BUS_PCI_HW_H
#define KERNEL_BUS_PCI_HW_H

#include <kernel/base.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define BAR_MEM_SPACE 0x0
#define BAR_IO_SPACE 0x1

//
// PCI Configuration Space
//

// Command Register
volatile union pci_command_reg {
  uint16_t raw;
  struct {
    uint16_t io_space : 1;          // I/O space
    uint16_t mem_space : 1;         // memory space
    uint16_t bus_master : 1;        // bus master
    uint16_t : 3;                   // reserved
    uint16_t parity_err_resp : 1;   // parity error response
    uint16_t : 1;                   // reserved
    uint16_t serr_enable : 1;       // SERR# enable
    uint16_t : 1;                   // reserved
    uint16_t int_disable : 1;       // interrupt disable
    uint16_t : 5;                   // reserved
  };
};
static_assert(sizeof(union pci_command_reg) == 2);

// Status Register
volatile union pci_status_reg {
  uint16_t raw;
  struct {
    uint16_t : 3;                 // reserved
    uint16_t int_status : 1;      // interrupt status
    uint16_t cap_list : 1;        // capabilities list (always 1)
    uint16_t : 3;                 // reserved
    uint16_t master_parity : 1;   // master data parity error
    uint16_t : 2;                 // reserved
    uint16_t sig_target_abrt : 1; // signalled target abort
    uint16_t rcv_target_abrt : 1; // received target abort
    uint16_t rcv_master_abrt : 1; // recevied master abort
    uint16_t sig_system_err : 1;  // signalled system error
    uint16_t parity_err : 1;      // detected parity error
  };
};
static_assert(sizeof(union pci_status_reg) == 2);

struct pci_header {
  // dword 0
  uint32_t vendor_id : 16;
  uint32_t device_id : 16;
  // dword 1
  union pci_command_reg command;
  union pci_status_reg status;
  // dword 2
  uint32_t rev_id : 8;
  uint32_t prog_if : 8;
  uint32_t subclass : 8;
  uint32_t class_code : 8;
  // dword 3
  uint32_t cache_line_sz : 8;
  uint32_t : 8;
  uint32_t type : 7;
  uint32_t multifn : 1;
  uint32_t bist : 8;
};
static_assert(sizeof(struct pci_header) == 16);

// Header Type 0 - a general device
struct pci_header_normal {
  // dword 0-3
  struct pci_header common;        // common header fields
  // dword 4-9
  uint32_t bars[6];                // base address registers
  // dword 10
  uint32_t cis_ptr;                // cardbus CIS pointer
  // dword 11
  uint32_t subsys_vendor_id : 16;  // subsystem vendor id
  uint32_t subsys_id : 16;         // subsystem id
  // dword 12
  uint32_t exp_rom_addr;           // expansion rom base address
  // dword 13
  uint32_t cap_ptr : 8;            // capabilties pointer
  uint32_t : 24;                   // reserved
  // dword 14
  uint32_t : 32;                   // reserved
  // dword 15
  uint32_t int_line : 8;           // interrupt line
  uint32_t int_pin : 8;            // interrupt pin
  uint32_t : 16;                   // reserved
};
static_assert(sizeof(struct pci_header_normal) == 64);

// Header Type 1 - a PCI-to-PCI bridge
struct pci_header_bridge {
  // dword 0-3
  struct pci_header common;        // common header fields
  // dword 4
  uint32_t bar0;                   // base address register 0
  // dword 5
  uint32_t bar1;                   // base address register 1
  // dword 6
  uint32_t primary_bus : 8;        // primary bus number
  uint32_t secondary_bus : 8;      // secondary bus number
  uint32_t subordinate_bus : 8;    // subordinate bus number
  uint32_t sec_latency_timer : 8;  // secondary latency timer
  // dword 7
  uint32_t io_base : 8;            // I/O base
  uint32_t io_limit : 8;           // I/O limit
  uint32_t sec_status : 16;        // secondary status
  // dword 8
  uint32_t mem_base : 16;          // memory base
  uint32_t mem_limit : 16;         // memory limit
  // dword 9
  uint32_t prefetch_mem_base : 16; // prefetchable memory base
  uint32_t prefetch_mem_limit : 16;// prefetchable memory limit
  // dword 10
  uint32_t prefetch_base_upper;    // prefetchable base upper 32 bits
  // dword 11
  uint32_t prefetch_limit_upper;   // prefetchable limit upper 32 bits
  // dword 12
  uint32_t io_base_upper : 16;     // I/O base upper 16 bits
  uint32_t io_limit_upper : 16;    // I/O limit upper 16 bits
  // dword 13
  uint32_t cap_ptr : 8;            // capabilties pointer
  uint32_t : 24;                   // reserved
  // dword 14
  uint32_t exp_rom_addr;           // expansion rom base address
  // dword 15
  uint32_t int_line : 8;           // interrupt line
  uint32_t int_pin : 8;            // interrupt pin
  uint32_t bridge_ctrl : 16;       // bridge control
};
static_assert(sizeof(struct pci_header_bridge) == 64);

//
// PCI Capability Structs
//

typedef volatile struct pci_cap_msix {
  // dword 0
  uint32_t id : 8;
  uint32_t next_ofst : 8;
  uint32_t tbl_sz: 11;
  uint32_t : 3;
  uint32_t fn_mask : 1;
  uint32_t en : 1;
  // dword 1
  uint32_t bir : 3;
  uint32_t tbl_ofst : 29;
  // dword 2
  uint32_t pb_bir : 3;
  uint32_t pb_ofst : 29;
} pci_cap_msix_t;

typedef volatile struct pci_msix_entry {
  // dword 0 & 1
  uint64_t msg_addr;
  // dword 2
  uint32_t msg_data;   // destination vector
  // dword 3
  uint32_t masked : 1;
  uint32_t : 31;
} pci_msix_entry_t;

#endif
