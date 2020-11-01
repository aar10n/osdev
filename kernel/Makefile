#
# Kernel Makefile
#

# kernel
kernel += entry.asm sched.asm smpboot.asm \
	acpi.c dev.c main.c panic.c percpu.c \
	process.c scheduler.c smpboot.c timer.c

# kernel/bus
kernel += bus/pci.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/idt.asm \
	cpu/io.asm cpu/exception.asm \
	cpu/gdt.c cpu/idt.c

# kernel/device
kernel += device/apic.c device/hpet.c \
	device/ioapic.c device/pic.c device/pit.c

#kernel/mm
kernel += mm/heap.c mm/mm.c mm/vm.c


# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)
