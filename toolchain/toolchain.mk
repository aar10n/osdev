# -------------- #
#   Toolchain    #
# -------------- #

ARCH ?= x86_64

CROSS_PREFIX ?= $(TOOLCHAIN_DIR)/bin/$(TARGET)-

CC = $(CROSS_PREFIX)gcc
CXX = $(CROSS_PREFIX)g++
AS = $(CROSS_PREFIX)as
LD = $(CROSS_PREFIX)ld
OBJCOPY = $(CROSS_PREFIX)objcopy
OBJDUMP = $(CROSS_PREFIX)objdump
READELF = $(CROSS_PREFIX)readelf
SIZE = $(CROSS_PREFIX)size
STRIP = $(CROSS_PREFIX)strip
STRINGS = $(CROSS_PREFIX)strings
RANLIB = $(CROSS_PREFIX)ranlib

NASM ?= nasm

CLANG ?= clang
CLANGXX ?= clang++
LLD_LINK ?= lld-link

GDB ?= gdb
QEMU ?= qemu-system-$(ARCH)

include toolchain/arch/$(ARCH).mk

# -------------- #
#  QEMU Options  #
# -------------- #

QEMU_CPU_CORES ?= 1
QEMU_CPU_THREADS ?= 2
QEMU_MEM ?= 256M

QEMU_OPTIONS ?= \
	-cpu $(QEMU_CPU) \
	-smp cores=$(QEMU_CPU_CORES),threads=$(QEMU_CPU_THREADS),sockets=1 \
	-m $(QEMU_MEM) \
	-machine q35 \
	-bios $(BUILD_DIR)/OVMF_$(WINARCH).fd \
	-drive file=$(BUILD_DIR)/osdev.img,id=boot,format=raw,if=none \
	-device ahci,id=ahci \
	-device qemu-xhci,id=xhci \
	$(QEMU_EXTRA_OPTIONS)

ifeq ($(QEMU_DEBUG),1)
QEMU_OPTIONS += \
	-serial file:$(BUILD_DIR)/kernel.log \
	-global isa-debugcon.iobase=0x402 \
	-debugcon file:$(BUILD_DIR)/uefi_debug.log \
	-trace events=$(BUILD_DIR)/traces
endif
