#
# Kernel Makefile
#

# kernel
kernel += entry.asm main.c panic.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/debug.asm \
	cpu/exception.asm cpu/interrupt.asm \
	cpu/io.asm cpu/exception.c cpu/idt.c \
	cpu/interrupt.c cpu/percpu.c

# kernel/device
kernel += device/pic.c

#kernel/mm
kernel += mm/heap.c mm/mm.c mm/vm.c


# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)