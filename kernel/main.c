//
// Created by Aaron Gill-Braun on 2019-04-17.
//

#include <drivers/ata_pio.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <drivers/serial.h>
#include <kernel/cpu/asm.h>
#include <multiboot.h>

#include <fs/ext2/ext2.h>
#include <fs/fs.h>

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/idt.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <kernel/acpi.h>
#include <kernel/bus/pci.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/pit.h>
#include <kernel/cpu/rtc.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/time.h>

extern uintptr_t initial_directory;
extern uintptr_t test_val;
extern uintptr_t ap_start;

void main(multiboot_info_t *mbinfo) {
  pde_t *initial_pd = (pde_t *) initial_directory;
  pde_t *initial_pd_alt = (pde_t *) &initial_directory;

  install_idt();
  install_gdt();

  pic_remap(0x20, 0x28);
  pic_disable();

  rtc_init(RTC_MODE_TIMER);
  init_serial(COM1);

  system_info_t *sys_info = acpi_get_sysinfo();
  ioapic_init(sys_info);
  apic_init(sys_info->apic_base);

  ioapic_set_mask(0, 1, 0);
  // ioapic_set_mask(0, 8, 0);

  init_keyboard();

  enable_interrupts();

  cpuinfo_t info;
  cpuinfo(&info);

  multiboot_info_t *mb = kmalloc(sizeof(multiboot_info_t));
  memcpy(mb, mbinfo, sizeof(multiboot_header_t));

  kclear();
  kprintf("Kernel loaded!\n");

  // kassertf(mbinfo->mods_count == 01, "failed to load initrd");
  // mb_module_t *rd_module = (mb_module_t *)(mbinfo->mods_addr);
  // uint32_t rd_start = phys_to_virt(rd_module->mod_start);
  // uint32_t rd_end = phys_to_virt(rd_module->mod_end);
  // uint32_t rd_size = rd_module->mod_end - rd_module->mod_start;
  //
  // uint8_t *initrd = (uint8_t *) rd_start;

  kprintf("\n");
  kprintf("Kernel Start: %p (%p)\n", kernel_start, virt_to_phys(kernel_start));
  kprintf("Kernel End: %p (%p)\n", kernel_end, virt_to_phys(kernel_end));
  kprintf("Kernel Size: %d KiB\n", (kernel_end - kernel_start) / 1024);
  kprintf("\n");

  kprintf("Lower Memory: %d KiB\n", mb->mem_lower);
  kprintf("Upper Memory: %d KiB\n", mb->mem_upper);
  kprintf("\n");

  // align the kernel_end address
  uintptr_t kernel_aligned = align(virt_to_phys(kernel_end), 0x1000);
  // uintptr_t kernel_aligned = (virt_to_phys(kernel_end) + 0x1000) & 0xFF00000;
  kprintf("kernel_aligned %p\n", phys_to_virt(kernel_aligned));
  // set aside 1mb for the watermark allocator heap
  // uintptr_t base_addr = kernel_aligned + 0x100000;
  kprintf("start_addr %p\n", phys_to_virt(kernel_aligned));
  size_t mem_size = (mbinfo->mem_upper * 1024) - (kernel_aligned);
  kprintf("base address: %p | size: %u MiB\n", kernel_aligned, mem_size / (1024 * 1024));

  paging_init();
  mem_init(kernel_aligned, mem_size);
  kheap_init();

  // init_keyboard();
  init_ata();

  kprintf("\n");

  kprintf("has sse: %d\n", has_sse());
  if (has_sse()) {
    kprintf("enabling sse\n");
    enable_sse();
  }

  kprintf("has long mode: %d\n", has_long_mode());

  // tasking_init();
  // int ret = fork();
  // kprintf("fork() -> %#x | getpid() -> %#x\n", ret, getpid());

  // fs_type_t ext2 = {
  //   .name = "ext2",
  //   .mount = ext2_mount };
  // fs_register_type(&ext2);

  // pci_enumerate_busses();
  // pci_device_t *device = pci_locate_device(PCI_STORAGE_CONTROLLER, PCI_IDE_CONTROLLER);
  // pci_print_debug_device(device);
}
