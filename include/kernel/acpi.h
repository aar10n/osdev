//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <base.h>
#include <cpu/cpu.h>

// common acpi header
#define acpi_header            \
  struct {                     \
    char signature[4];         \
    uint32_t length;           \
    uint8_t revision;          \
    uint8_t checksum;          \
    char oem_id[6];            \
    char oem_table_id[8];      \
    uint32_t oem_revision;     \
    uint32_t creator_id;       \
    uint32_t creator_revision; \
  }

/* ------ Root System Description Pointer ------ */

typedef struct packed {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_addr;
  // acpi version 2.0
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t ext_cheksum;
  uint8_t reserved[3];
} acpi_rsdp_t;

typedef struct {
  acpi_header;
} acpi_rsdt_t;

typedef struct {
  acpi_header;
} acpi_xsdt_t;

typedef acpi_header acpi_header_t;

/* ------ Multiple APIC Description Table ------ */

typedef struct {
  acpi_header;
  uint32_t apic_phys_addr;
  uint32_t flags;
} acpi_madt_t;

typedef struct {
  uint8_t type;
  uint8_t length;
} acpi_madt_entry_t;

#define MADT_FLAGS_ACTIVE_LOW 2
#define MADT_FLAGS_LEVEL_TRIGGERED 8

// Processor local APIC
#define MADT_ENTRY_LAPIC 0
typedef struct packed {
  uint8_t type;               // type = 0
  uint8_t length;             // record length
  uint8_t processor_id;       // processor id
  uint8_t apic_id;            // local apic id
  struct {
    uint32_t enabled : 1;     // processor is enabled
    uint32_t reserved : 31;   // remaining unused flags
  } flags;
} madt_entry_local_apic_t;

// I/O APIC
#define MADT_ENTRY_IOAPIC 1
typedef struct packed {
  uint8_t type;            // type = 1
  uint8_t length;          // record length
  uint8_t io_apic_id;      // i/o apic id
  uint8_t reserved;        // reserved
  uint32_t io_apic_addr;   // i/o apic address
  uint32_t interrupt_base; // global system interrupt base
} madt_entry_io_apic_t;

// Interrupt source override
#define MADT_ENTRY_ISO 2
typedef struct packed {
  uint8_t type;           // type = 2
  uint8_t length;         // record length
  uint8_t bus_source;     // bus source
  uint8_t irq_source;     // irq source
  uint32_t sys_interrupt; // global system interrupt
  uint16_t flags;         // flags
} madt_entry_iso_t;

// Non-maskable interrupts
#define MADT_ENTRY_NMI 4
typedef struct packed {
  uint8_t type;         // type = 4
  uint8_t length;       // record length
  uint8_t processor_id; // processor id (0xFF = all cpu's)
  uint16_t flags;       // flags
  uint8_t lint_num;     // LINT# (0 or 1)
} madt_entry_nmi_t;

// Local APIC Address Override
#define MADT_ENTRY_LAPIC_AO 5
typedef struct packed {
  uint8_t type;       // type = 5
  uint8_t length;     // record length
  uint16_t reserved;  // reserved
  uint64_t phys_addr; // 64-bit lapic address
} madt_entry_lapic_ao_t;

extern system_info_t *system_info;

void acpi_init();

#endif
