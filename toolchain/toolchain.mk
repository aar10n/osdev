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

SSH ?= ssh
RSYNC ?= rsync

include toolchain/arch/$(ARCH).mk

ifeq ($(DEBUG),1)
EDK2_BUILD = DEBUG
else
EDK2_BUILD = RELEASE
endif

# -------------- #
#  QEMU Options  #
# -------------- #

DEBUG_DIR ?= $(BUILD_DIR)

QEMU_NCORES ?= 1
QEMU_NTHREADS ?= 2
QEMU_SMP ?= cores=$(QEMU_NCORES),threads=$(QEMU_NTHREADS),sockets=1
QEMU_MEM ?= 256M
QEMU_MACHINE ?= q35

QEMU_DEVICES ?= -device ahci,id=ahci -device qemu-xhci,id=xhci

ifeq ($(QEMU_DEBUG),1)
QEMU_DEBUG_OPTIONS += \
	-serial file:$(DEBUG_DIR)/kernel.log \
	-global isa-debugcon.iobase=0x402 \
	-debugcon file:$(DEBUG_DIR)/uefi_debug.log
endif

ifdef QEMU_TRACE_FILE
QEMU_DEBUG_OPTIONS += \
	-trace file=$(QEMU_TRACE_FILE)
endif

QEMU_OPTIONS ?= \
	-cpu $(QEMU_CPU) \
	-smp $(QEMU_SMP) \
	-m $(QEMU_MEM) \
	-machine $(QEMU_MACHINE) \
	-bios $(BUILD_DIR)/OVMF_$(WINARCH).fd \
	-drive file=$(BUILD_DIR)/osdev.img,id=boot,format=raw,if=none \
	$(QEMU_DEVICES) \
	$(QEMU_EXTRA_OPTIONS) \
	$(QEMU_DEBUG_OPTIONS)

