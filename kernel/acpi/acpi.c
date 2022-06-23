//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#include <acpi/acpi.h>

#include <cpu/io.h>
#include <device/ioapic.h>

#include <printf.h>
#include <panic.h>

size_t acpi_num_tables = 0;
acpi_table_header_t **acpi_tables = NULL;
uint8_t acpi_interrupt_sources[256];

void acpi_parse_madt();
void acpi_parse_mcfg();
void acpi_parse_hpet();
void acpi_parse_dmar();

void acpi_print_address(acpi_address_t *addr);


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

  acpi_parse_madt();
  acpi_parse_mcfg();
  acpi_parse_hpet();
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

void acpi_parse_madt() {
  acpi_table_header_t *madt_ptr = acpi_locate_table(ACPI_SIGNATURE_MADT);
  if (madt_ptr == NULL) {
    panic("ACPI MADT not found");
  }

  uint32_t enabled_count = 0;
  uint32_t online_capable_count = 0;
  acpi_madt_header_t *header = (void *) madt_ptr;
  if (header->flags & ACPI_MADT_FLAG_PCAT_COMPAT) {
    kprintf("ACPI: MADT PCAT compatibility mode\n");

    // ISA IRQs identity map to GSIs
    disable_legacy_pic();
    for (int i = 0; i < 16; i++) {
      acpi_interrupt_sources[i] = i;
    }
  }

  acpi_madt_entry_t *entries = (void *)((uint64_t) header + sizeof(acpi_madt_header_t));
  acpi_madt_entry_t *entry = entries;
  while ((uintptr_t) entry < (uintptr_t) offset_ptr(madt_ptr, header->header.length)) {
    if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
      acpi_madt_local_apic_t *local_apic = (void *) entry;
      if ((local_apic->flags & ACPI_MADT_APIC_FLAG_ENABLED) != 0) {
        enabled_count++;
      } else if ((local_apic->flags & ACPI_MADT_APIC_FLAG_ONLINE_CAP) != 0) {
        online_capable_count++;
      }
    } else if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      acpi_madt_io_apic_t *io_apic = (void *) entry;
      if (io_apic->global_interrupt_base < 256) {
        kprintf("ACPI: IOAPIC[%d] address=%p GSI=%d\n", io_apic->io_apic_id, io_apic->address, io_apic->global_interrupt_base);
        register_ioapic(io_apic->io_apic_id, io_apic->address, io_apic->global_interrupt_base);
      } else {
        kprintf("ACPI: IOAPIC[%d] GSI Base out of range\n", io_apic->io_apic_id);
      }
    } else if (entry->type == ACPI_MADT_TYPE_INT_SRC) {
      acpi_madt_iso_t *iso = (void *) entry;
      kassert(iso->bus == 0);
      kassert(iso->global_system_interrupt < 256);
      acpi_interrupt_sources[iso->global_system_interrupt] = iso->source;
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
    for (int gsi = 0; gsi < 16; gsi++) {
      int source = acpi_interrupt_sources[gsi];
      if (gsi != source) {
        kprintf("ACPI: ISA interrupt %d remapped to IRQ%d\n", source, gsi);
      }
      ioapic_set_irq_vector(source, gsi);
    }
  }

  kprintf("ACPI: %d processors enabled, %d online capable\n", enabled_count, online_capable_count);
}

void acpi_parse_mcfg() {
  acpi_table_header_t *mcfg_ptr = acpi_locate_table(ACPI_SIGNATURE_MCFG);
  if (mcfg_ptr == NULL) {
    panic("ACPI MCFG not found");
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
  }
}

void acpi_parse_hpet() {
  acpi_table_header_t *hpet_ptr = acpi_locate_table(ACPI_SIGNATURE_HPET);
  if (hpet_ptr == NULL) {
    panic("ACPI HPET not found");
  }

  acpi_hpet_header_t *header = (void *) hpet_ptr;
  kprintf("HPET\n");
  kprintf("Event Timer Block ID: %#x\n", header->event_timer_block_id);
  kprintf("  Hardware Rev ID: %d\n", header->event_timer_block_id & 0xFF);
  kprintf("  Number of Comparators: %d\n", (header->event_timer_block_id >> 8) & 0xF);
  kprintf("  COUNT_SIZE_CAP counter size: %d\n", (header->event_timer_block_id >> 12) & 1);
  kprintf("  Legacy Replacement IRQ Routing: %d\n", (header->event_timer_block_id >> 14) & 1);
  kprintf("  PCI Vendor ID: %d\n", header->event_timer_block_id >> 16);
  kprintf("Address:\n");
  acpi_print_address(&header->base_address);
  kprintf("Number: %d\n", header->hpet_number);
  kprintf("Minimum Clock Ticks: %d\n", header->minimum_tick);
  kprintf("Page Protection: %d\n", header->page_protection);
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

void acpi_print_address(acpi_address_t *addr) {
  kprintf("  Address Space ID: %d\n", addr->address_space_id);
  kprintf("  Bit Width: %d\n", addr->register_bit_width);
  kprintf("  Bit Offset: %d\n", addr->register_bit_offset);
  kprintf("  Access Size: %d\n", addr->access_size);
  kprintf("  Address: %p\n", addr->address);
}