//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#ifndef KERNEL_ACPI_ACPI_H
#define KERNEL_ACPI_ACPI_H

#include <base.h>

extern uint16_t enabled_apic_count;
extern uint16_t online_capable_apic_count;
extern uint16_t total_apic_count;
extern uint8_t apic_id_map[];


#define ACPI_SIGNATURE_RSDP   SIGNATURE_64('R', 'S', 'D', ' ', 'P', 'T', 'R', ' ')
#define ACPI_SIGNATURE_FADT   SIGNATURE_32('F', 'A', 'C', 'P')
#define ACPI_SIGNATURE_MADT   SIGNATURE_32('A', 'P', 'I', 'C')
#define ACPI_SIGNATURE_MCFG   SIGNATURE_32('M', 'C', 'F', 'G')
#define ACPI_SIGNATURE_HPET   SIGNATURE_32('H', 'P', 'E', 'T')
#define ACPI_SIGNATURE_DMAR   SIGNATURE_32('D', 'M', 'A', 'R')
#define ACPI_SIGNATURE_XSDT   SIGNATURE_32('X', 'S', 'D', 'T')

typedef struct packed acpi_table_header {
  uint32_t signature;
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} acpi_table_header_t;

//

#define ACPI_ADDRESS_SPACE_MEMORY  0
#define ACPI_ADDRESS_SPACE_IO      1
#define ACPI_ADDRESS_SPACE_PCI_ALL 2
#define ACPI_ADDRESS_SPACE_PCI_BAR 3

typedef struct packed acpi_address {
  uint8_t address_space_id;
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t access_size;
  uint64_t address;
} acpi_address_t;

typedef struct packed acpi_rsdp {
  uint64_t signature;
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;
  // revision 2
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} acpi_rsdp_t;

// Fixed ACPI Description Table

typedef struct packed acpi_fadt {
  acpi_table_header_t header;
  uint32_t firmware_ctrl;
  uint32_t dsdt;
  uint8_t : 8; // reserved
  uint8_t preferred_pm_profile;
  uint16_t sci_int;
  uint32_t smi_cmd;
  uint8_t acpi_enable;
  uint8_t acpi_disable;
  uint8_t s4bios_req;
  uint8_t pstate_cnt;
  uint32_t pm1a_evt_blk;
  uint32_t pm1b_evt_blk;
  uint32_t pm1a_cnt_blk;
  uint32_t pm1b_cnt_blk;
  uint32_t pm2_cnt_blk;
  uint32_t pm_tmr_blk;
  uint32_t gpe0_blk;
  uint32_t gpe1_blk;
  uint8_t pm1_evt_len;
  uint8_t pm1_cnt_len;
  uint8_t pm2_cnt_len;
  uint8_t pm_tmr_len;
  uint8_t gpe0_blk_len;
  uint8_t gpe1_blk_len;
  uint8_t gpe1_base;
  uint8_t cst_cnt;
  uint16_t p_lvl2_lat;
  uint16_t p_lvl3_lat;
  uint16_t flush_size;
  uint16_t flush_stride;
  uint8_t duty_offset;
  uint8_t duty_width;
  uint8_t day_alarm;
  uint8_t mon_alarm;
  uint8_t century;
  uint16_t iapc_boot_arch;
  uint8_t : 8; // reserved
  uint32_t flags;
  acpi_address_t reset_reg;
  uint8_t reset_value;
  uint16_t arm_boot_arch;
  uint8_t fadt_minor_version;
  uint64_t x_firmware_ctrl;
  uint64_t x_dsdt;
  acpi_address_t x_pm1a_evt_blk;
  acpi_address_t x_pm1b_evt_blk;
  acpi_address_t x_pm1a_cnt_blk;
  acpi_address_t x_pm1b_cnt_blk;
  acpi_address_t x_pm2_cnt_blk;
  acpi_address_t x_pm_tmr_blk;
  acpi_address_t x_gpe0_blk;
  acpi_address_t x_gpe1_blk;
  acpi_address_t sleep_control_reg;
  acpi_address_t sleep_status_reg;
  uint64_t hypervisor_vendor_id;
} acpi_fadt_t;

// Multiple APIC Description Table

#define ACPI_MADT_TYPE_LOCAL_APIC    0
#define ACPI_MADT_TYPE_IO_APIC       1
#define ACPI_MADT_TYPE_INT_SRC       2
#define ACPI_MADT_TYPE_NMI_INT_SRC   3
#define ACPI_MADT_TYPE_LAPIC_NMI     4
#define ACPI_MADT_TYPE_APIC_OVERRIDE 5

#define ACPI_MADT_FLAG_PCAT_COMPAT     (1 << 0)

#define ACPI_MADT_APIC_FLAG_ENABLED    (1 << 0)
#define ACPI_MADT_APIC_FLAG_ONLINE_CAP (1 << 1)

typedef struct packed acpi_madt_header {
  acpi_table_header_t header;
  uint32_t local_apic_address;
  uint32_t flags;
} acpi_madt_header_t;

typedef struct packed acpi_madt_entry {
  uint8_t type;
  uint8_t length;
} acpi_madt_entry_t;

typedef struct packed acpi_madt_local_apic {
  acpi_madt_entry_t header;
  uint8_t acpi_processor_uid; // deprecated
  uint8_t apic_id;
  uint32_t flags;
} acpi_madt_local_apic_t;

typedef struct packed acpi_madt_io_apic {
  acpi_madt_entry_t header;
  uint8_t io_apic_id;
  uint8_t reserved;
  uint32_t address;
  uint32_t global_interrupt_base;
} acpi_madt_io_apic_t;

typedef struct packed acpi_madt_iso {
  acpi_madt_entry_t header;
  uint8_t bus;
  uint8_t source;
  uint32_t global_system_interrupt;
  uint16_t flags;
} acpi_madt_iso_t;

typedef struct packed acpi_madt_nmi_source {
  acpi_madt_entry_t header;
  uint16_t flags;
  uint32_t global_system_interrupt;
} acpi_madt_nmi_source_t;

typedef struct packed acpi_madt_apic_nmi {
  acpi_madt_entry_t header;
  uint8_t acpi_processor_uid;
  uint16_t flags;
  uint8_t local_apic_lint;
} acpi_madt_apic_nmi_t;

// MCFG Table

typedef struct packed acpi_mcfg_header {
  acpi_table_header_t header;
  uint64_t reserved;
} acpi_mcfg_header_t;

typedef struct packed acpi_mcfg_entry {
  uint64_t base_address;
  uint16_t segment_group_number;
  uint8_t start_bus_number;
  uint8_t end_bus_number;
  uint32_t reserved;
} acpi_mcfg_entry_t;

// HPET Table

typedef struct packed acpi_hpet_header {
  acpi_table_header_t header;
  uint32_t event_timer_block_id;
  acpi_address_t base_address;
  uint8_t hpet_number;
  uint16_t minimum_tick;
  uint8_t page_protection;
} acpi_hpet_header_t;

// DMA Remapping Table

typedef struct packed acpi_dmar_header {
  acpi_table_header_t header;
  uint8_t host_address_width;
  uint8_t flags;
  uint8_t reserved[10];
} acpi_dmar_header_t;

typedef struct packed acpi_dmar_entry {
  uint16_t type;
  uint16_t length;
} acpi_dmar_entry_t;

// DMA-Remapping Hardware unit Definition (DRHD)
typedef struct packed acpi_dmar_drhd {
  acpi_dmar_entry_t header;
  uint8_t flags;
  uint8_t size;
  uint16_t segment_number;
  uint64_t register_base_address;
} acpi_dmar_drhd_t;

typedef struct packed acpi_dmar_dhdt_dev_scope {
  uint8_t type;
  uint8_t length;
  uint8_t flags;
  uint8_t reserved;
  uint8_t enumeration_id;
  uint8_t bus_number;
  uint8_t path[];
} acpi_dmar_dhdt_dev_scope_t;

//

void acpi_early_init();
acpi_table_header_t *acpi_locate_table(uint32_t signature);

clock_t acpi_read_pm_timer();

#endif
