# Kernel sources
$(call register,kernel,KERNEL)

# kernel/
kernel += entry.asm memory.asm smpboot.asm syscall.asm thread.asm \
	clock.c console.c errno.c event.c ipc.c init.c irq.c loader.c \
	main.c mutex.c panic.c printf.c process.c semaphore.c signal.c \
	smpboot.c spinlock.c string.c syscall.c thread.c timer.c queue.c \
	ipi.c

# kernel/acpi
kernel += acpi/acpi.c

# kernel/bus
kernel += bus/pci.c bus/pci_tables.c bus/pcie.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/idt.asm cpu/io.asm cpu/exception.asm \
	cpu/cpu.c cpu/gdt.c cpu/idt.c cpu/per_cpu.c

# kernel/device
kernel += device/apic.c device/hpet.c \
	device/ioapic.c device/pit.c

# kernel/gui
kernel += gui/screen.c

# kernel/mm
kernel += mm/init.c mm/heap.c mm/pgtable.c mm/pmalloc.c mm/vmalloc.c

# kernel/sched
kernel += sched/sched.c sched/fprr.c

# kernel/usb
kernel += usb/hid.c usb/hid-report.c usb/hid-usage.c \
	usb/keyboard.c usb/mouse.c usb/scsi.c usb/usb.c \
	usb/xhci.c
