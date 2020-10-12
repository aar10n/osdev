//
// Created by Aaron Gill-Braun on 2020-09-18.
//

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel/acpi.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/asm.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/mm.h>
#include <kernel/mm/paging.h>

#define get_header(ptr) ((acpi_header_t *) ptr)

#define EBDA_START 0x80000
#define RSDP_REGION_START 0xE0000
#define RSDP_REGION_SIZE 0x1FFFF

const char *sig_rsdp = "RSD PTR ";

const char *sig_madt = "APIC";
const char *sig_rsdt = "RSDT";

static core_desc_t cores[256] = {};
static ioapic_desc_t ioapics[16] = {};

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

// Root System Description Pointer

acpi_rsdp_t *locate_rsdp() {
  // there are two locations where the rsdp might be:
  //   - the first 1KiB of the ebda
  //   - somewhere between 0xE0000 and 0xFFFFF

  kprintf("[acpi] locating rsdp...\n");

  // lets first try finding it in the first KiB of the ebda
  uint8_t *ebda_base = (uint8_t *) EBDA_START;
  int upper_bound = 1024;
locate_rsdp:
  for (int i = 0; i < upper_bound; i++) {
    char *buf = (char *) (ebda_base + i);
    if (is_signature(buf, sig_rsdp, 8)) {
      kprintf("[acpi] found rsdp\n");

      acpi_rsdp_t *rsdp = (acpi_rsdp_t *) (ebda_base + i);
      if (!is_rsdp_valid(rsdp)) {
        kprintf("[acpi] rsdp checksum failed\n");
        return NULL;
      }
      return rsdp;
    }
  }

  if (upper_bound == 1024) {
    // if that didn't work, lets look in the larger region
    ebda_base = (uint8_t *) RSDP_REGION_START;
    upper_bound = RSDP_REGION_SIZE;
    goto locate_rsdp;
  }

  kprintf("[acpi] failed to find rsdp\n");
  return NULL;
}

// Multiple APIC Description Table

system_info_t *iterate_madt(acpi_madt_t *madt) {
  cpu_info_t cpu_info;
  get_cpu_info(&cpu_info);

  system_info_t *info = kmalloc(sizeof(system_info_t));
  info->apic_base = madt->local_apic_addr;
  info->bsp_id = cpu_info.ebx.local_apic_id;
  info->core_count = 0;
  info->ioapic_count = 0;

  uint32_t length = madt->length - sizeof(acpi_madt_t);
  acpi_madt_entry_t *entry = (void *) (madt + 1);

  kprintf("\nMultiple APIC Description Table\n");
  kprintf("-------------------------------\n");

  memset(cores, 0, sizeof(cores));
  memset(ioapics, 0, sizeof(ioapics));

  uint8_t max_apic_id = 0;
  uint8_t core_count = 0;
  uint8_t ioapic_count = 0;
  irq_source_t *last_source = NULL;
  while (length > 0) {
    if (entry->type == MADT_ENTRY_LOCAL_APIC) {
      madt_entry_local_apic_t *e = (void *) entry;
      core_count++;

      uint32_t version = *((uint32_t *) 0xFEE00030);

      apic_desc_t *apic = kmalloc(sizeof(apic_desc_t));
      apic->id = e->apic_id;
      apic->version = version & 0xFF;
      apic->max_lvt = (version >> 16) & 0xFF;
      apic->flags.bsp = e->apic_id == info->bsp_id;
      apic->flags.enabled = apic->flags.bsp && 1;
      apic->flags.has_eoi_supress = (version >> 24) & 1;

      uint8_t id = e->apic_id;
      cores[id].id = e->processor_id;
      cores[id].local_apic = apic;

      if (id > max_apic_id) {
        max_apic_id = id;
      }

      kprintf("Processor Local APIC\n");
      kprintf("  Processor ID: %d\n", e->processor_id);
      kprintf("  APIC ID: %d\n", e->apic_id);
      kprintf("  Enabled: %d\n", e->flags.enabled);
    } else if (entry->type == MADT_ENTRY_IO_APIC) {
      madt_entry_io_apic_t *e = (void *) entry;
      ioapic_count++;

      *((uint32_t *) (e->io_apic_addr + IOREGSEL)) = IOAPIC_REG_VERSION;
      uint32_t version = *((uint32_t *) (e->io_apic_addr + IOREGWIN));

      uint8_t id = e->io_apic_id;
      ioapics[id].id = id;
      ioapics[id].version = version & 0xFF;
      ioapics[id].max_rentry = (version >> 16) & 0xFF;
      ioapics[id].address = e->io_apic_addr;
      ioapics[id].base = e->interrupt_base;
      ioapics[id].sources = NULL;

      kprintf("I/O APIC\n");
      kprintf("  APIC ID: %d\n", e->io_apic_id);
      kprintf("  APIC Address: %p\n", phys_to_virt(e->io_apic_addr));
      kprintf("  Global System Interrupt Base: %p\n", e->interrupt_base);
    } else if (entry->type == MADT_ENTRY_ISO) {
      madt_entry_iso_t *e = (void *) entry;

      irq_source_t *source = kmalloc(sizeof(irq_source_t));
      source->source_irq = e->irq_source;
      source->dest_interrupt = e->sys_interrupt;
      source->flags = e->flags;
      source->next = NULL;

      if (last_source != NULL) {
        last_source->next = source;
      } else {
        ioapics[0].sources = source;
      }

      last_source = source;

      kprintf("Interrupt Source Override\n");
      kprintf("  Bus Source: %d\n", e->bus_source);
      kprintf("  IRQ Source: %d\n", e->irq_source);
      kprintf("  Global System Interrupt: %d\n", e->sys_interrupt);
      kprintf("  Flags: %b\n", e->flags);
    } else if (entry->type == MADT_ENTRY_NMI) {
      madt_entry_nmi_t *e = (void *) entry;

      kprintf("Non-maskable interrupts\n");
      kprintf("  Processor ID: %d\n", e->processor_id);
      kprintf("  Flags: %b\n", e->flags);
      kprintf("  LINT#: %d\n", e->lint_num);
    } else {
      kprintf("Unknown\n");
      kprintf("  Type: %d\n", entry->type);
    }

    length -= entry->length;
    entry = (acpi_madt_entry_t *) ((uintptr_t) entry + entry->length);
  }

  core_desc_t *sys_cores = kmalloc(core_count * sizeof(core_desc_t));
  memcpy(sys_cores, cores, core_count * sizeof(core_desc_t));

  ioapic_desc_t *sys_ioapics = kmalloc(ioapic_count * sizeof(ioapic_desc_t));
  memcpy(sys_ioapics, ioapics, ioapic_count * sizeof(ioapic_desc_t));

  info->core_count = core_count;
  info->cores = sys_cores;
  info->ioapic_count = ioapic_count;
  info->ioapics = sys_ioapics;

  kprintf("\nTotal Cores: %d\n", core_count);
  kprintf("-------------------------------\n\n");

  return info;
}


//

void *locate_header(acpi_rsdt_t *rsdt, const char *signature) {
  uint32_t entries = (rsdt->length - sizeof(acpi_rsdt_t)) / sizeof(uintptr_t);
  uintptr_t *pointers = (uintptr_t *) (rsdt + 1);

  kprintf("[acpi] locating header %s...\n", signature);

  for (int i = 0; i < entries; i++) {
    uintptr_t ptr = pointers[i];
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

system_info_t *acpi_get_sysinfo() {
  // initial_pd = (pde_t *) &initial_directory;

  acpi_rsdp_t *rsdp = locate_rsdp();
  acpi_rsdt_t *rsdt = (void *) rsdp->rsdt_addr;
  if (!is_header_valid((acpi_header_t *) rsdt)) {
    kprintf("[acpi] rsdt checksum failed\n");
    return NULL;
  }

  acpi_madt_t *madt = locate_header(rsdt, sig_madt);
  system_info_t *sys_info = iterate_madt(madt);
  return sys_info;
}