#
# Kernel Makefile
#

# kernel
kernel += boot.asm \
	task.asm acpi.c \
	main.c panic.c \
	task.c time.c

# kernel/bus
kernel += bus/pci.c

# kernel/cpu
kernel += cpu/asm.asm cpu/cpu.asm \
	cpu/exception.asm cpu/interrupt.asm \
	cpu/exception.c cpu/apic.c cpu/asm.c \
	cpu/gdt.c cpu/idt.c cpu/interrupt.c \
	cpu/ioapic.c cpu/pic.c cpu/pit.c \
	cpu/rtc.c

# kernel/mm
kernel += mm/paging.asm \
	mm/cache.c mm/heap.c \
	mm/mm.c mm/paging.c \
	mm/stack.c

# kernel/smp
kernel += smp/entry.asm \
	smp/smpboot.c

# kernel/vga
kernel += vga/vga.c
