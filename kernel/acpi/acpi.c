//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#include <acpi/acpi.h>
#include <acpi/pm_timer.h>

#include <cpu/io.h>

#include <device/apic.h>
#include <device/ioapic.h>
#include <device/hpet.h>

// #include <bus/pcie.h>
#include <bus/pci_v2.h>

#include <mm.h>
#include <init.h>
#include <irq.h>
#include <printf.h>
#include <panic.h>

#define ISA_NUM_IRQS 16
#define MAX_NUM_APICS 128

void acpi_parse_fadt();
void acpi_parse_madt();
void acpi_parse_mcfg();
void acpi_parse_hpet();
void acpi_parse_dmar();

void acpi_print_address(acpi_address_t *addr);

size_t acpi_num_tables = 0;
acpi_table_header_t **acpi_tables = NULL;
acpi_fadt_t *acpi_global_fadt = NULL;
uint16_t enabled_apic_count = 0;
uint16_t online_capable_apic_count = 0;
uint16_t total_apic_count = 0;
uint8_t apic_id_map[MAX_NUM_APICS];

//

void remap_acpi_tables(void *data) {
  uintptr_t phys_base = (uintptr_t) acpi_global_fadt;
  kassert(phys_base % PAGE_SIZE == 0);

  uintptr_t virt_base = (uintptr_t) _vmap_mmio(phys_base, PAGE_SIZE, 0);
  _vmap_get_mapping(virt_base)->name = "acpi (fadt)";

  size_t fadt_offset = ((uintptr_t) acpi_global_fadt) - phys_base;
  acpi_global_fadt = (void *)(virt_base + fadt_offset);
}

//

void acpi_early_init() {
  kassert(boot_info_v2->acpi_ptr != 0);
  kassert(acpi_num_tables == 0);
  kassert(acpi_tables == NULL);

  acpi_rsdp_t *rsdp = (void *)((uint64_t) boot_info_v2->acpi_ptr);
  if (rsdp->signature != ACPI_SIGNATURE_RSDP) {
    panic("ACPI RSDP signature mismatch");
  } else if (rsdp->revision == 0) {
    panic("ACPI 1.0 not supported");
  }

  acpi_table_header_t *xsdt = (void *)((uint64_t) rsdp->xsdt_address);
  if (xsdt->signature != ACPI_SIGNATURE_XSDT) {
    panic("ACPI XSDT signature mismatch");
  }

  acpi_num_tables = (xsdt->length - sizeof(acpi_table_header_t)) / sizeof(uint64_t);
  acpi_tables = (void *)((uint64_t) xsdt + sizeof(acpi_table_header_t));

  kprintf("ACPI %s\n", rsdp->revision == 0 ? "1.0" : "2.0");
  kprintf("  RSDT %p (v%d %6s)\n", rsdp->rsdt_address, rsdp->revision, rsdp->oem_id);
  kprintf("  XSDT %p (v%d %6s)\n", rsdp->xsdt_address, xsdt->revision, xsdt->oem_id);
  for (size_t i = 0; i < acpi_num_tables; i++) {
    acpi_table_header_t *table = (void *)((uint64_t) acpi_tables[i]);
    kprintf("  %4s %p (v%d %6s)\n", &table->signature, table, table->revision, table->oem_id);
  }

  acpi_parse_fadt();
  acpi_parse_madt();
  acpi_parse_mcfg();
  acpi_parse_hpet();

  register_init_address_space_callback(remap_acpi_tables, NULL);
}

acpi_table_header_t *acpi_locate_table(uint32_t signature) {
  kassert(acpi_tables != NULL);
  for (size_t i = 0; i < acpi_num_tables; i++) {
    acpi_table_header_t *table = (void *)((uint64_t) acpi_tables[i]);
    if (table->signature == signature) {
      return table;
    }
  }
  return NULL;
}

void acpi_parse_fadt() {
  acpi_table_header_t *fadt_ptr = acpi_locate_table(ACPI_SIGNATURE_FADT);
  if (fadt_ptr == NULL) {
    kprintf("ACPI: warning: FADT not found");
    return;
  }

  acpi_fadt_t *fadt = (void *) fadt_ptr;
  acpi_global_fadt = fadt;
  register_acpi_pm_timer();
}

void acpi_parse_madt() {
  acpi_table_header_t *madt_ptr = acpi_locate_table(ACPI_SIGNATURE_MADT);
  if (madt_ptr == NULL) {
    panic("ACPI: error: MADT not found");
  }

  acpi_madt_iso_t *isa_irq_overrides[ISA_NUM_IRQS] = {0};
  acpi_madt_header_t *header = (void *) madt_ptr;
  if (header->flags & ACPI_MADT_FLAG_PCAT_COMPAT) {
    kprintf("ACPI: MADT PCAT compatibility mode\n");

    // ISA IRQs identity map to GSIs
    disable_legacy_pic();
    for (int i = 0; i < ISA_NUM_IRQS; i++) {
      isa_irq_overrides[i] = NULL;
    }
  }

  acpi_madt_entry_t *entries = (void *)((uint64_t) header + sizeof(acpi_madt_header_t));
  acpi_madt_entry_t *entry = entries;
  while ((uintptr_t) entry < (uintptr_t) offset_ptr(madt_ptr, header->header.length)) {
    if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
      acpi_madt_local_apic_t *local_apic = (void *) entry;
      if ((local_apic->flags & ACPI_MADT_APIC_FLAG_ENABLED) != 0) {
        enabled_apic_count++;
      } else if ((local_apic->flags & ACPI_MADT_APIC_FLAG_ONLINE_CAP) != 0) {
        online_capable_apic_count++;
      }

      kassert(total_apic_count < MAX_NUM_APICS);
      apic_id_map[total_apic_count++] = local_apic->apic_id;
      register_apic(local_apic->apic_id);
    } else if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      acpi_madt_io_apic_t *io_apic = (void *) entry;
      if (io_apic->global_interrupt_base < 256) {
        register_ioapic(io_apic->io_apic_id, io_apic->address, io_apic->global_interrupt_base);
      } else {
        kprintf("ACPI: IOAPIC[%d] GSI Base out of range\n", io_apic->io_apic_id);
      }
    } else if (entry->type == ACPI_MADT_TYPE_INT_SRC) {
      acpi_madt_iso_t *iso = (void *) entry;
      kassert(iso->bus == 0);
      kassert(iso->global_system_interrupt < ISA_NUM_IRQS);
      isa_irq_overrides[iso->source] = iso;
    } else {
      kprintf("ACPI: ");
      switch (entry->type) {
        case ACPI_MADT_TYPE_LOCAL_APIC: kprintf("Processor Local APIC\n"); break;
        case ACPI_MADT_TYPE_IO_APIC: kprintf("I/O APIC\n"); break;
        case ACPI_MADT_TYPE_INT_SRC: kprintf("Interrupt Source Override\n"); break;
        case ACPI_MADT_TYPE_NMI_INT_SRC: kprintf("Non-maskable Interrupt Source\n"); break;
        case ACPI_MADT_TYPE_LAPIC_NMI: kprintf("Local APIC NMI\n"); break;
        case ACPI_MADT_TYPE_APIC_OVERRIDE: kprintf("Local APIC Address Override\n"); break;
        case 6: kprintf("I/O SAPIC\n"); break;
        case 7: kprintf("Local SAPIC\n"); break;
        case 8: kprintf("Platform Interrupt Source\n"); break;
        case 9: kprintf("Processor Local x2APIC\n"); break;
        default: kprintf("Entry Type: %x\n", entry->type); break;
      }
    }

    entry = offset_ptr(entry, entry->length);
  }

  // finish re-routing legacy interrupts
  if (header->flags & ACPI_MADT_FLAG_PCAT_COMPAT) {
    for (int isa_irq = 0; isa_irq < ISA_NUM_IRQS; isa_irq++) {
      acpi_madt_iso_t *iso = isa_irq_overrides[isa_irq];
      if (iso == NULL) {
        continue;
      }

      uint8_t dest_irq = iso->global_system_interrupt;
      if (isa_irq != dest_irq) {
        kprintf("ACPI: ISA IRQ%d remapped to IRQ%d\n", isa_irq, dest_irq);
      }
      irq_override_isa_interrupt(isa_irq, dest_irq, iso->flags);
    }
  }

  kprintf("ACPI: %d processors enabled, %d online capable\n", enabled_apic_count, online_capable_apic_count);
}

void acpi_parse_mcfg() {
  acpi_table_header_t *mcfg_ptr = acpi_locate_table(ACPI_SIGNATURE_MCFG);
  if (mcfg_ptr == NULL) {
    kprintf("ACPI: warning: MCFG not found\n");
    return;
  }

  acpi_mcfg_header_t *header = (void *) mcfg_ptr;
  acpi_mcfg_entry_t *entries = (void *)((uint64_t) header + sizeof(acpi_mcfg_header_t));
  size_t num_entries = (header->header.length - sizeof(acpi_mcfg_header_t)) / sizeof(acpi_mcfg_entry_t);
  for (size_t i = 0; i < num_entries; i++) {
    acpi_mcfg_entry_t *entry = &entries[i];
    kprintf("Entry:\n");
    kprintf("  Address: %p\n", entry->base_address);
    kprintf("  PCI Segment Group Number: %d\n", entry->segment_group_number);
    kprintf("  Start Bus Number: %d\n", entry->start_bus_number);
    kprintf("  End Bus Number: %d\n", entry->end_bus_number);

    uint16_t number = entry->segment_group_number;
    uint8_t bus_start = entry->start_bus_number;
    uint8_t bus_end = entry->end_bus_number;
    uint64_t address = entry->base_address;
    register_pci_segment_group(number, bus_start, bus_end, address);
    // register_pcie_segment_group(number, bus_start, bus_end, address);
  }
}

void acpi_parse_hpet() {
  acpi_table_header_t *hpet_ptr = acpi_locate_table(ACPI_SIGNATURE_HPET);
  if (hpet_ptr == NULL) {
    kprintf("ACPI: warning: HPET not found\n");
    return;
  }

  acpi_hpet_header_t *header = (void *) hpet_ptr;
  uint8_t id = header->hpet_number;
  uintptr_t address = header->base_address.address;
  uint8_t num_counters = ((header->event_timer_block_id >> 8) & 0x1F) + 1;
  kprintf("ACPI: HPET[%d] address=%p counters=%d\n", id, address, num_counters);
  register_hpet(id, address, header->minimum_tick);
}

void acpi_parse_dmar() {
  acpi_table_header_t *dmar_ptr = acpi_locate_table(ACPI_SIGNATURE_DMAR);
  if (dmar_ptr == NULL) {
    return;
  }

  acpi_dmar_header_t *header = (void *) dmar_ptr;
  acpi_dmar_entry_t *entry = offset_ptr(header, sizeof(acpi_dmar_header_t));
  acpi_dmar_entry_t *end_ptr = offset_ptr(dmar_ptr, header->header.length);
  while (entry < end_ptr) {
    switch (entry->type) {
      case 0: kprintf("DRHD\n"); break;
      case 1: kprintf("RMRR\n"); break;
      case 2: kprintf("ATSR\n"); break;
      case 3: kprintf("RHSA\n"); break;
      case 4: kprintf("ANDD\n"); break;
      case 5: kprintf("SATC\n"); break;
      case 6: kprintf("SIDP\n"); break;
      default: kprintf("Unknown %d\n", entry->type); break;
    }

    if (entry->type == 0) {
      acpi_dmar_drhd_t *drhd = (void *) entry;
      kprintf("  Flags: %d\n", drhd->flags);
      kprintf("  Size: %d 4KB Pages\n", 1 << (drhd->size & 0xF));
      kprintf("  Segment Number: %d\n", drhd->segment_number);
      kprintf("  Register Base Address: %p\n", drhd->register_base_address);

      kprintf("  Device Scope:\n");
      acpi_dmar_dhdt_dev_scope_t *scope = offset_ptr(drhd, sizeof(acpi_dmar_drhd_t));
      acpi_dmar_dhdt_dev_scope_t *end_scope = offset_ptr(drhd, drhd->header.length);
      while (scope < end_scope) {
        const char *type = "";
        switch (scope->type) {
          case 1: type = "PCI Endpoint Device"; break;
          case 2: type = "PCI Sub-hierarchy"; break;
          case 3: type = "IOAPIC"; break;
          case 4: type = "HPET"; break;
          case 5: type = "ACPI Namespace Device"; break;
          default: type = "Unknown"; break;
        }

        kprintf("    Type: %s\n", type);
        kprintf("    Length: %d\n", scope->length);
        kprintf("    Flags: %d\n", scope->flags);
        kprintf("    Enumeration ID: %d\n", scope->enumeration_id);
        kprintf("    Bus Number: %d\n", scope->bus_number);

        size_t path_len = scope->length - sizeof(acpi_dmar_dhdt_dev_scope_t);
        if (path_len > 0) {
          kprintf("    Path: ");
        }

        for (size_t i = 0; i < path_len; i++) {
          kprintf("%d ", scope->path[i]);
        }

        if (path_len > 0) {
          kprintf("\n");
        }
        kprintf("    ----\n");
        scope = offset_ptr(scope, scope->length);
      }
    }

    entry = offset_ptr(entry, entry->length);
  }
}

//

clock_t acpi_read_pm_timer() {
  uint8_t addr_type = acpi_global_fadt->x_pm_tmr_blk.address_space_id;
  if (addr_type == 0x00) {
    // memory
    volatile uint32_t *pm_tmr = (void *) acpi_global_fadt->x_pm_tmr_blk.address;
    return *pm_tmr;
  } else if (addr_type == 0x01) {
    // io
    return indw((uint16_t) acpi_global_fadt->x_pm_tmr_blk.address);
  } else {
    panic("acpi: unsupported access type: %d\n", addr_type);
  }
}

//

void acpi_print_address(acpi_address_t *addr) {
  kprintf("  Address Space ID: %d\n", addr->address_space_id);
  kprintf("  Bit Width: %d\n", addr->register_bit_width);
  kprintf("  Bit Offset: %d\n", addr->register_bit_offset);
  kprintf("  Access Size: %d\n", addr->access_size);
  kprintf("  Address: %p\n", addr->address);
}
