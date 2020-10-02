#
# Kernel Makefile
#

# kernel
kernel += entry.asm main.c panic.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/io.asm

# kernel/device
kernel +=

#kernel/mm
kernel += mm/heap.c mm/mm.c



# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)
