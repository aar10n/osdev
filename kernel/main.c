//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <printf.h>
#include <panic.h>

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/heap.h>

#include <acpi.h>
#include <percpu.h>
#include <smpboot.h>
#include <syscall.h>
#include <timer.h>
#include <scheduler.h>

#include <drivers/serial.h>
#include <drivers/ahci.h>

#include <device/apic.h>
#include <device/ioapic.h>
#include <device/pic.h>

#include <loader.h>
#include <fs.h>
#include <fs/proc/proc.h>
#include <fs/utils.h>
#include <fs/path.h>
#include <fs/blkdev.h>
#include <fat/fat.h>

#include <bus/pcie.h>
#include <usb/usb.h>
#include <usb/scsi.h>


boot_info_t *boot_info;

//
// Kernel launch process
//

void launch() {
  sti();
  timer_init();

  kprintf("[pid %d] launch\n", ID);
  fs_init();

  pcie_init();
  pcie_discover();

  usb_init();

  // usb_device_t *drive = usb_get_device(0);

  // fs_lsdir("/dev");

  // // inode_t *inode = kmalloc(sizeof(inode_t));
  // fs_t *fatfs = fat_mount(blkdev, NULL);
  // fatfs->device = blkdev;
  // fat_locate(fatfs, NULL, 0);

  fs_mount(fatfs_driver, "/dev/sda", "/test");

  fs_lsdir("/test");

  kprintf("done!\n");
  thread_block();
}

//
// Kernel entry
//

__used void kmain(boot_info_t *info) {
  boot_info = info;
  percpu_init();
  enable_sse();

  serial_init(COM1);
  kprintf("[kernel] initializing\n");

  setup_gdt();
  setup_idt();

  kheap_init();

  mm_init();
  vm_init();

  acpi_init();

  pic_init();
  apic_init();
  ioapic_init();

  syscalls_init();
  // smp_init();

  // root process
  process_t *root = create_root_process(launch);
  scheduler_init(root);
}

__used void ap_main() {
  percpu_init();
  enable_sse();

  kprintf("[CPU#%d] initializing\n", ID);

  setup_gdt();
  setup_idt();

  vm_init();
  apic_init();
  ioapic_init();

  kprintf("[CPU#%d] done!\n", ID);
}
