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

#include <interval_tree.h>
#include <usb/hid.h>


boot_info_t *boot_info;

//
// Kernel launch process
//

// noreturn void poll_keyboard() {
//   usb_device_t *kbd = usb_get_device(0);
//   uint8_t *buffer = kmalloc(8);
//
//   while (true) {
//     uint64_t now = timer_now();
//     usb_add_transfer(kbd, USB_IN, (void *) heap_ptr_phys(buffer), 8);
//   }
// }

void launch() {
  sti();
  timer_init();

  kprintf("[pid %d] launch\n", ID);
  fs_init();

  pcie_init();
  pcie_discover();

  usb_init();

  kprintf("done!\n");

  thread_sleep(5000000);

  // usb_device_t *kbd = usb_get_device(0);

  uint8_t *buffer = kmalloc(8);
  usb_device_t *kbd = usb_get_device(0);
  usb_add_transfer(kbd, USB_IN, (void *) vm_virt_to_phys(buffer), 8);
  kprintf("starting transfer\n");
  usb_start_transfer(kbd, USB_IN);

  thread_sleep(10000000);
  usb_start_transfer(kbd, USB_IN);
  // kprintf("awaiting transfer\n");
  // usb_await_transfer(kbd, USB_IN);
  //
  // for (int i = 0; i < 8; i++) {
  //   kprintf("%#x ", (int) buffer[i]);
  // }
  // kprintf("\n");
  // kprintf("transfer done!\n");

  // usb_device_t *drive = usb_get_device(0);
  // blkdev_t *blkdev = blkdev_init(drive, scsi_read, scsi_write);
  //
  // inode_t *inode = kmalloc(sizeof(inode_t));
  // fs_t *fs = kmalloc(sizeof(fs_t));
  //
  // fs_t *fatfs = fat_mount(blkdev, NULL);

  // fs->device = blkdev;
  // fs->data = data;
  //
  // fat_locate(fs, NULL, 0);

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
