# common definitions
MAKEFLAGS += rR

ifeq ($(ARCH),x86_64)
WINARCH = X64
else
$(error "Unsupported architecture: $(ARCH)")
endif

# -------------- #
#   Toolchain    #
# -------------- #

CROSS_PREFIX ?= $(TOOL_ROOT)/bin/$(TOOLCHAIN)-

CC = $(CROSS_PREFIX)gcc
CXX = $(CROSS_PREFIX)g++
AS = $(CROSS_PREFIX)as
LD = $(CROSS_PREFIX)ld
AR = $(CROSS_PREFIX)ar
OBJCOPY = $(CROSS_PREFIX)objcopy
OBJDUMP = $(CROSS_PREFIX)objdump
READELF = $(CROSS_PREFIX)readelf
SIZE = $(CROSS_PREFIX)size
STRIP = $(CROSS_PREFIX)strip
STRINGS = $(CROSS_PREFIX)strings
RANLIB = $(CROSS_PREFIX)ranlib

CLANG ?= clang
CLANGXX ?= clang++
LLD_LINK ?= lld-link
NASM ?= nasm

GDB ?= gdb
QEMU ?= qemu-system-$(ARCH)
SSH ?= ssh
RSYNC ?= rsync

EDK2_BUILD ?= RELEASE

# -------------- #
#  QEMU Options  #
# -------------- #

ifeq ($(ARCH),x86_64)
QEMU_CPU ?= Nehalem
endif

DEBUG_DIR ?= $(BUILD_DIR)

QEMU_NCORES ?= 1
QEMU_NTHREADS ?= 2
QEMU_SMP ?= cores=$(QEMU_NCORES),threads=$(QEMU_NTHREADS),sockets=1
QEMU_MEM ?= 256M
QEMU_MACHINE ?= q35

QEMU_DEVICES ?= -device ahci,id=ahci -device qemu-xhci,id=xhci

QEMU_SERIAL_DEVICES ?= \
	-serial file:$(BUILD_DIR)/kernel.log \
	-serial file:$(BUILD_DIR)/loader.log
#	-serial pty

ifeq ($(QEMU_DEBUG),1)
QEMU_DEBUG_OPTIONS ?= \
	-global isa-debugcon.iobase=0x402 \
	-debugcon file:$(DEBUG_DIR)/uefi_debug.log

ifdef QEMU_TRACE_FILE
QEMU_DEBUG_OPTIONS += -trace events=$(QEMU_TRACE_FILE),file=$(BUILD_DIR)/events.out
endif
endif

QEMU_OPTIONS ?= \
	-cpu $(QEMU_CPU) \
	-smp $(QEMU_SMP) \
	-m $(QEMU_MEM) \
	-machine $(QEMU_MACHINE) \
	-bios $(BUILD_DIR)/OVMF_$(WINARCH).fd \
	-drive file=$(BUILD_DIR)/osdev.img,id=boot,format=raw,if=none \
	$(QEMU_DEVICES) \
	$(QEMU_SERIAL_DEVICES) \
	$(QEMU_DEBUG_OPTIONS) \
	$(QEMU_EXTRA_OPTIONS)
