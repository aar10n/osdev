//
// Created by Aaron Gill-Braun on 2021-02-18.
//

#ifndef KERNEL_BUS_PCIE_H
#define KERNEL_BUS_PCIE_H

#include <base.h>
#include <bus/pci.h>

#define PCIE_MMIO_SIZE 0x10000000 // 256 MiB

typedef struct pcie_bar {
  uint8_t num : 3;       // bar number
  uint8_t kind : 1;      // bar kind
  uint8_t type : 2;      // memory type
  uint8_t prefetch : 1;  // prefetchable
  uint8_t : 1;           // reserved
  uint64_t addr;         // base address
  uint64_t size;         // memory size
  struct pcie_bar *next; // next bar
} pcie_bar_t;

typedef struct pcie_cap {
  uint8_t id;            // capability id
  uint8_t offset;        // offset to cap
  struct pcie_cap *next; // next cap ptr
} pcie_cap_t;

typedef struct pcie_device {
  uint16_t device_id;
  uint16_t vendor_id;

  uint8_t bus;
  uint8_t device : 5;
  uint8_t function : 3;

  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t int_line;
  uint8_t int_pin;

  uint16_t subsystem;
  uint16_t subsystem_vendor;

  pcie_bar_t *bars;
  pcie_cap_t *caps;
  struct pcie_device *next;
} pcie_device_t;


typedef struct pcie_list_head {
  uint8_t class_code;
  uint8_t subclass;
  pcie_device_t *first;
  pcie_device_t *last;
  struct pcie_list_head *next;
} pcie_list_head_t;


//
// -------- Configuration Space --------
//

// Command Register
typedef union {
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
} pcie_command_reg_t;
static_assert(sizeof(pcie_command_reg_t) == 2);

// Status Register
typedef volatile union {
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
} pcie_status_reg_t;
static_assert(sizeof(pcie_status_reg_t) == 2);

typedef struct {
  // dword 0
  uint32_t vendor_id : 16;
  uint32_t device_id : 16;
  // dword 1
  pcie_command_reg_t command;
  pcie_status_reg_t status;
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
} pcie_header_t;
static_assert(sizeof(pcie_header_t) == 16);


// Header Type 0
typedef struct {
  // dword 0-3
  pcie_header_t common;            // common header fields
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
 uint32_t : 32;                    // reserved
 // dword 15
 uint32_t int_line : 8;            // interrupt line
 uint32_t int_pin : 8;             // interrupt pin
 uint32_t : 16;                    // reserved
} pcie_header_normal_t;
static_assert(sizeof(pcie_header_normal_t) == 64);

// Header Type 1
typedef struct {
  // dword 0-3
  pcie_header_t common;            // common header fields
  // dword 4-5
  uint32_t bars[2];                // base address registers
  // dword 6
} pcie_header_bridge_t;


void pcie_init();
void pcie_discover();
pcie_device_t *pcie_locate_device(uint8_t class_code, uint8_t subclass, int prog_if);
pcie_bar_t *pcie_get_bar(pcie_device_t *device, int bar);

void pcie_print_device(pcie_device_t *device);

#endif
