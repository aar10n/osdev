//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <base.h>
#include <string.h>
#include <printf.h>

#include <acpi.h>
#include <panic.h>
#include <mm.h>

#include <device/apic.h>
#include <device/ioapic.h>

#include <cpuid.h>

#include <bus/pcie.h>

#define KERNEL_OFFSET  0xFFFFFF8000000000
#define get_header(ptr) ((acpi_header_t *) ptr)
#define kernel_phys_to_virt(x) (KERNEL_OFFSET + (x))

const char *sig_rsdp = "RSD PTR ";

const char *sig_hpet = "HPET";
const char *sig_madt = "APIC";
const char *sig_mcfg = "MCFG";
const char *sig_rsdt = "RSDT";
const char *sig_xsdt = "XSDT";

static core_desc_t cores[256] = {};
static ioapic_desc_t ioapics[16] = {};

static acpi_xsdt_t *xsdt;

system_info_t *system_info;

//

int is_signature(const char *buf, const char *sig, uint32_t len) {
  for (int i = 0; i < len; i++) {
    if (buf[i] != sig[i]) {
      return 0;
    }
  }
  return 1;
}

// Checksum validation

int checksum(const uint8_t *bytes, uint32_t length) {
  uint8_t sum = 0;
  for (int i = 0; i < length; i++) {
    sum += bytes[i];
  }
  return sum == 0;
}

int is_rsdp_valid(acpi_rsdp_t *rsdp) {
  return checksum((uint8_t *) rsdp, sizeof(acpi_rsdp_t));
}

int is_header_valid(acpi_header_t *header) {
  return checksum((uint8_t *) header, header->length);
}

// Multiple APIC Description Table

void get_apic_info(system_info_t *info, acpi_madt_t *madt) {
  if (madt == NULL) {
    panic("[acpi] could not find apic information");
  }

  uint32_t a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) {
    panic("[acpi] cpuid failed");
  }


  info->apic_base = madt->apic_phys_addr;
  info->bsp_id = (b >> 24) & 0xFF;
  info->core_count = 0;
  info->ioapic_count = 0;

  uint32_t length = madt->length - sizeof(acpi_madt_t);
  acpi_madt_entry_t *entry = (void *) (madt + 1);

  // kprintf("\nMultiple APIC Description Table\n");
  // kprintf("-------------------------------\n");

  memset(cores, 0, sizeof(cores));
  memset(ioapics, 0, sizeof(ioapics));

  uint8_t max_apic_id = 0;
  uint8_t core_count = 0;
  uint8_t ioapic_count = 0;
  irq_source_t *last_source = NULL;
  while (length > 0) {
    size_t offset;
    if (entry->type == MADT_ENTRY_LAPIC) {
      madt_entry_local_apic_t *e = (void *) entry;
      offset = sizeof(madt_entry_local_apic_t);
      core_count++;

      apic_desc_t *apic = kmalloc(sizeof(apic_desc_t));
      apic->id = e->apic_id;
      apic->flags.bsp = e->apic_id == info->bsp_id;
      apic->flags.enabled = apic->flags.bsp && 1;

      uint8_t id = e->apic_id;
      cores[id].id = e->processor_id;
      cores[id].local_apic = apic;
      if (id > max_apic_id) {
        max_apic_id = id;
      }

      // kprintf("Processor Local APIC\n");
      // kprintf("  Processor ID: %d\n", e->processor_id);
      // kprintf("  APIC ID: %d\n", e->apic_id);
      // kprintf("  Enabled: %d\n", e->flags.enabled);
    } else if (entry->type == MADT_ENTRY_IOAPIC) {
      madt_entry_io_apic_t *e = (void *) entry;
      offset = sizeof(madt_entry_io_apic_t);
      ioapic_count++;

      uint8_t id = e->io_apic_id;
      ioapics[id].id = id;
      ioapics[id].version = 0;
      ioapics[id].max_rentry = 0;
      ioapics[id].phys_addr = e->io_apic_addr;
      ioapics[id].virt_addr = 0;
      ioapics[id].int_base = e->interrupt_base;
      ioapics[id].sources = NULL;

      // kprintf("I/O APIC\n");
      // kprintf("  I/O APIC ID: %d\n", e->io_apic_id);
      // kprintf("  I/O APIC Address: %p\n", virt_addr);
      // kprintf("  Global System Interrupt Base: %d\n", e->interrupt_base);
    } else if (entry->type == MADT_ENTRY_ISO) {
      madt_entry_iso_t *e = (void *) entry;
      offset = sizeof(madt_entry_iso_t);

      irq_source_t *source = kmalloc(sizeof(irq_source_t));
      source->source_irq = e->irq_source;
      source->dest_int = e->sys_interrupt;
      source->flags = e->flags;
      source->next = NULL;

      if (last_source != NULL) {
        last_source->next = source;
      } else {
        ioapics[0].sources = source;
      }
      last_source = source;

      // kprintf("Interrupt Source Override\n");
      // kprintf("  Bus Source: %d\n", e->bus_source);
      // kprintf("  IRQ Source: %d\n", e->irq_source);
      // kprintf("  Global System Interrupt: %d\n", e->sys_interrupt);
      // kprintf("  Flags: %b\n", e->flags);
    } else if (entry->type == MADT_ENTRY_NMI) {
      madt_entry_nmi_t *e = (void *) entry;
      offset = sizeof(madt_entry_nmi_t);

      // kprintf("Non-maskable Interrupt\n");
      // kprintf("  Processor ID: %d\n", e->processor_id);
      // kprintf("  Flags: %b\n", e->flags);
      // kprintf("  LINT#: %d\n", e->lint_num);
    } else if (entry->type == MADT_ENTRY_LAPIC_AO) {
      madt_entry_lapic_ao_t *e = (void *) entry;
      offset = sizeof(madt_entry_lapic_ao_t);
      info->apic_base = e->phys_addr;

      // kprintf("Local APIC Address Override\n");
      // kprintf("  APIC Address: %p\n", e->phys_addr);
    } else {
      kprintf("[acpi] unknown madt entry type: %d\n", entry->type);
      kprintf("[acpi] something went wrong\n");
      break;
    }

    entry = (acpi_madt_entry_t *) ((uintptr_t) entry + offset);
    length -= offset;
  }

  info->core_count = core_count;
  info->cores = cores;
  info->ioapic_count = ioapic_count;
  info->ioapics = ioapics;
  // kprintf("\nTotal Cores: %d\n", core_count);
  // kprintf("-------------------------------\n\n");
}

// HPET Description Table

void get_hpet_info(system_info_t *info, acpi_hpetdt_t *hpetdt) {
  if (hpetdt == NULL) {
    info->hpet = NULL;
    return;
  }

  hpet_desc_t *hpet = kmalloc(sizeof(hpet_desc_t));
  hpet->block_id.raw = hpetdt->hpet_block_id;
  hpet->number = hpetdt->hpet_number;
  hpet->min_clock_tick = hpetdt->min_clock_tick;
  hpet->phys_addr = hpetdt->base_addr.address;
  hpet->virt_addr = 0;
  info->hpet = hpet;
}

// MCFG Table

void get_mcfg_info(system_info_t *info, acpi_mcfg_header_t *mcfg) {
  mcfg_entry_t *entries = mcfg->entries;
  uint32_t num_entries = (mcfg->length - sizeof(acpi_mcfg_header_t)) / sizeof(mcfg_entry_t);

  for (int i = 0; i < num_entries; i++) {
    mcfg_entry_t entry = entries[i];
    kprintf("--- entry %d ---\n", i);
    kprintf("addr: %p\n", entry.base_addr);
    kprintf("segment: %d\n", entry.segment);
    kprintf("bus_start: %d\n", entry.bus_start);
    kprintf("bus_end: %d\n", entry.bus_end);

    pcie_desc_t *pcie = kmalloc(sizeof(pcie_desc_t));
    pcie->phys_addr = entry.base_addr;
    pcie->virt_addr = 0;
    pcie->bus_start = entry.bus_start;
    pcie->bus_end = entry.bus_end;
    info->pcie = pcie;
    break;
  }
}

//

void *locate_header(const char *signature) {
  uint32_t entries = (xsdt->length - sizeof(acpi_xsdt_t)) / sizeof(uintptr_t);
  uintptr_t *pointers = (uintptr_t *) (xsdt + 1);

  kprintf("[acpi] locating header %s...\n", signature);

  for (int i = 0; i < entries; i++) {
    uintptr_t ptr = kernel_phys_to_virt(pointers[i]);
    acpi_header_t *header = get_header(ptr);
    if (is_signature(header->signature, signature, 4)) {
      kprintf("[acpi] %s header found\n", signature);

      if (!is_header_valid(header)) {
        kprintf("[acpi] %s checksum failed\n", signature);
        return NULL;
      }
      return header;
    }
  }

  kprintf("[acpi] failed to find header %s\n", signature);
  return NULL;
}


void acpi_init() {
  memory_map_t *mem = boot_info->mem_map;
  if (boot_info->acpi_table == 0) {
    panic("[acpi] acpi tables not found\n");
  }

  kprintf("[acpi] mapping acpi tables\n");
  size_t mapped_count = 0;
  memory_map_entry_t *region = mem->map;
  while ((uintptr_t) region < (uintptr_t) mem->map + mem->size) {
    if (region->type != MEMORY_ACPI) {
      region++;
      continue;
    }

    uintptr_t virt_addr = kernel_phys_to_virt(region->base);
    size_t size = align(region->size, PAGE_SIZE);

    unreachable;
    // vm_map_vaddr(virt_addr, region->base, size, 0);
    // kprintf("[acpi] mapped region (%p -> %p)\n",
    //         region->phys_addr, virt_addr);

    mapped_count++;
    region++;
  }

  if (mapped_count == 0) {
    panic("[acpi] failed to locate acpi regions");
  }

  acpi_rsdp_t *rsdp = (void *) kernel_phys_to_virt(boot_info->acpi_table);
  if (!checksum((uint8_t *) rsdp, sizeof(acpi_rsdp_t))) {
    panic("[acpi] rsdp checksum failed");
  }

  xsdt = (void *) kernel_phys_to_virt(rsdp->xsdt_address);
  if (!is_header_valid((acpi_header_t *) xsdt)) {
    panic("[acpi] rsdt checksum failed");
  }

  kprintf("[acpi] acpi tables mapped!\n");
  kprintf("[acpi] collecting system info\n");

  system_info = kmalloc(sizeof(system_info_t));
  acpi_madt_t *madt = locate_header(sig_madt);
  get_apic_info(system_info, madt);

  acpi_hpetdt_t *hpet = locate_header(sig_hpet);
  get_hpet_info(system_info, hpet);

  acpi_mcfg_header_t *mcfg = locate_header(sig_mcfg);
  get_mcfg_info(system_info, mcfg);

  kprintf("[acpi] done!\n");
}
