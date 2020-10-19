//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <string.h>
#include <stdio.h>

#include <acpi.h>
#include <panic.h>
#include <mm/heap.h>
#include <mm/vm.h>

#include <device/apic.h>
#include <device/ioapic.h>

#include <cpuid.h>

#define get_header(ptr) ((acpi_header_t *) ptr)

const char *sig_rsdp = "RSD PTR ";

const char *sig_hpet = "HPET";
const char *sig_madt = "APIC";
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


  info->apic_phys_addr = madt->apic_phys_addr;
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

      kprintf("[acpi] mapping ioapic\n");
      uintptr_t phys_addr = e->io_apic_addr;
      uintptr_t virt_addr = MMIO_BASE_VA + PAGE_SIZE;
      size_t ioapic_mmio_size = PAGE_SIZE;
      if (!vm_find_free_area(ABOVE, &virt_addr, ioapic_mmio_size)) {
        panic("[acpi] failed to map ioapic");
      }
      vm_map_vaddr(virt_addr, phys_addr, ioapic_mmio_size, PE_WRITE);

      *((uint32_t *) (virt_addr + IOREGSEL)) = IOAPIC_VERSION;
      uint32_t version = *((uint32_t *) (virt_addr + IOREGWIN));

      uint8_t id = e->io_apic_id;
      ioapics[id].id = id;
      ioapics[id].version = version & 0xFF;
      ioapics[id].max_rentry = (version >> 16) & 0xFF;
      ioapics[id].phys_addr = phys_addr;
      ioapics[id].virt_addr = virt_addr;
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
      info->apic_phys_addr = e->phys_addr;

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

  // map apic registers into virtual address space
  kprintf("[acpi] mapping local apic\n");
  uintptr_t phys_addr = info->apic_phys_addr;
  uintptr_t virt_addr = MMIO_BASE_VA;
  size_t apic_mmio_size = PAGE_SIZE;
  if (!vm_find_free_area(EXACTLY, &virt_addr, apic_mmio_size)) {
    panic("[acpi] failed to map local apic");
  }
  vm_map_vaddr(virt_addr, phys_addr, apic_mmio_size, PE_WRITE);
  info->apic_virt_addr = virt_addr;

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
  }

  kprintf("[acpi] mapping hpet\n");
  uintptr_t phys_addr = hpetdt->base_addr.address;
  uintptr_t virt_addr = MMIO_BASE_VA;
  size_t hpet_mmio_size = PAGE_SIZE;
  if (!vm_find_free_area(ABOVE, &virt_addr, hpet_mmio_size)) {
    panic("[acpi] failed to map hpet");
  }
  vm_map_vaddr(virt_addr, phys_addr, hpet_mmio_size, PE_WRITE);

  hpet_desc_t *hpet = kmalloc(sizeof(hpet_desc_t));
  hpet->block_id.raw = hpetdt->hpet_block_id;
  hpet->number = hpetdt->hpet_number;
  hpet->min_clock_tick = hpetdt->min_clock_tick;
  hpet->phys_addr = hpetdt->base_addr.address;
  hpet->virt_addr = virt_addr;

  info->hpet = hpet;
}

//

void *locate_header(const char *signature) {
  uint32_t entries = (xsdt->length - sizeof(acpi_xsdt_t)) / sizeof(uintptr_t);
  uintptr_t *pointers = (uintptr_t *) (xsdt + 1);

  kprintf("[acpi] locating header %s...\n", signature);

  for (int i = 0; i < entries; i++) {
    uintptr_t ptr = phys_to_virt(pointers[i]);
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
  memory_region_t *region = mem->mmap;
  while ((uintptr_t) region < (uintptr_t) mem->mmap + mem->mmap_size) {
    if (region->type != MEMORY_ACPI) {
      region++;
      continue;
    }

    uintptr_t virt_addr = phys_to_virt(region->phys_addr);
    size_t size = align(region->size, PAGE_SIZE);

    vm_map_vaddr(virt_addr, region->phys_addr, size, 0);
    // kprintf("[acpi] mapped region (%p -> %p)\n",
    //         region->phys_addr, virt_addr);

    mapped_count++;
    region++;
  }

  if (mapped_count == 0) {
    panic("[acpi] failed to locate acpi regions");
  }

  acpi_rsdp_t *rsdp = (void *) phys_to_virt(boot_info->acpi_table);
  if (!checksum((uint8_t *) rsdp, sizeof(acpi_rsdp_t))) {
    panic("[acpi] rsdp checksum failed");
  }

  xsdt = (void *) phys_to_virt(rsdp->xsdt_address);
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

  kprintf("[acpi] done!\n");
}
