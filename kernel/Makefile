#
# Kernel Makefile
#

INCLUDE-kernel = $(INCLUDE) \
	-Ithird-party/edk2/MdePkg/Include \
	-Ithird-party/edk2/MdePkg/Include/X64


# kernel
kernel += entry.asm main.c io.asm

# kernel-y
kernel-y = $(call objects,kernel,$(BUILD_DIR)/kernel)
