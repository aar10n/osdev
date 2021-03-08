#
# Kernel Makefile
#

CFLAGS-kernel = $(CFLAGS) -fstrict-volatile-bitfields

# kernel
kernel += entry.asm sched.asm smpboot.asm \
	syscall.asm user.asm acpi.c loader.c main.c \
	panic.c percpu.c printf.c process.c scheduler.c \
	smpboot.c syscall.c timer.c

# kernel/bus
kernel += bus/pci.c bus/pci_tables.c bus/pcie.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/idt.asm \
	cpu/io.asm cpu/exception.asm \
	cpu/gdt.c cpu/idt.c

# kernel/device
kernel += device/apic.c device/hpet.c \
	device/ioapic.c device/pic.c device/pit.c

# kernel/mm
kernel += mm/heap.c mm/mm.c mm/vm.c

# kernel/usb
kernel += usb/ehci.c usb/xhci.c


# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)
