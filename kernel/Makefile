# Kernel sources
$(call register,kernel,KERNEL)

KERNEL_CFLAGS = $(CFLAGS) -mcmodel=large -mno-red-zone -fno-stack-protector \
				-fno-omit-frame-pointer -fno-builtin-memset \
				$(KERNEL_DEFINES)

KERNEL_LDFLAGS = $(LDFLAGS) -Tlinker.ld -nostdlib -z max-page-size=0x1000 -L$(BUILD_DIR)

KERNEL_INCLUDE = $(INCLUDE) -Ilib -I$(TOOL_ROOT)/include

KERNEL_DEFINES = $(DEFINES) -D__KERNEL__


# kernel/
kernel += entry.asm isr.asm memory.asm smpboot.asm syscall.asm switch.asm \
	chan.c cond.c clock.c device.c errno.c exec.c init.c irq.c loadelf.c \
	loader.c lock.c main.c sched.c panic.c printf.c signal.c smpboot.c ipi.c \
	string.c syscall.c timer.c input.c kio.c tty.c tqueue.c proc.c  \
	percpu.c fs_utils.c mutex.c rwlock.c time.c

# kernel/acpi
kernel += acpi/acpi.c acpi/pm_timer.c

# kernel/bus
kernel += bus/pci.c bus/pci_tables.c bus/pcie.c bus/pci_v2.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/io.asm cpu/cpu.c cpu/gdt.c cpu/idt.c cpu/tcb.c

# kernel/debug
kernel += debug/debug.c debug/dwarf.c

# kernel/hw
kernel += hw/8250.c hw/8254.c hw/apic.c hw/hpet.c \
	hw/ioapic.c hw/pit.c hw/rtc.c

# kernel/gui
kernel += gui/screen.c

# kernel/mm
kernel += mm/init.c mm/heap.c mm/pgtable.c mm/pmalloc.c mm/vmalloc.c

# kernel/usb
kernel += usb/usb.c usb/xhci.c \
	usb/scsi.c usb/hid.c \
	usb/keyboard.c usb/mouse.c \
	usb/hid-report.c usb/hid-usage.c

# kernel/vfs
kernel += vfs/file.c vfs/fs.c vfs/path.c vfs/vcache.c vfs/ventry.c \
	vfs/vfs.c vfs/vnode.c vfs/vresolve.c
