#
# Kernel Makefile
#

# kernel
kernel += entry.asm main.c panic.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/idt.asm \
	cpu/io.asm cpu/exception.c \
	cpu/gdt.c cpu/idt.c cpu/percpu.c

# kernel/device
kernel += device/pic.c

#kernel/mm
kernel += mm/heap.c mm/mm.c mm/vm.c


# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)
