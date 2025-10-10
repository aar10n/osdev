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

CLANG = clang
CLANGXX = clang++
LLD_LINK = lld-link
NASM ?= nasm

GDB = gdb
QEMU = qemu-system-$(ARCH)
SSH = ssh
RSYNC = rsync

EDK2_BUILD = RELEASE

# -------------- #
#  QEMU Options  #
# -------------- #

ifeq ($(ARCH),x86_64)
QEMU_CPU = Nehalem
endif

DEBUG_DIR = $(BUILD_DIR)

QEMU_NCORES = 1
QEMU_NTHREADS = 2
QEMU_SMP = cores=$(QEMU_NCORES),threads=$(QEMU_NTHREADS),sockets=1
QEMU_MEM = 256M
QEMU_MACHINE = q35

QEMU_DEVICES = \
	-device ahci,id=ahci -device qemu-xhci,id=xhci \
	-rtc base=localtime,clock=vm

QEMU_SERIAL_DEVICES = \
	-serial $(or $(QEMU_SHELL_DEVICE),$(error QEMU_SHELL_DEVICE is not set)) \
	-serial file:$(BUILD_DIR)/kernel.log

