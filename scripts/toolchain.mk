ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
# ==== x86_64 ====
TARGET = x86_64-elf
WINARCH = X64
CROSS_PREFIX ?= $(TOOLCHAIN_DIR)/bin/x86_64-elf-
LDFLAGS += -m elf_x86_64

QEMU_CPU ?= Nehalem
QEMU_CPU_CORES ?= 1
QEMU_CPU_THREADS ?= 2
QEMU_MEM ?= 256M

# =================
else ifeq ($(ARCH),aarch64)
# ==== aarch64 ====
TARGET = aarch64-elf
WINARCH = AA64
$(error "Unsupported architecture: $(ARCH)")
# =================
else
$(error "Unknown architecture: $(ARCH)")
endif

# -------------- #
#  Dependencies  #
# -------------- #

# edk2
ifeq ($(call exists,$(EDK_DIR)),false)
$(info "error: edk2 not found [EDK_DIR = $(EDK_DIR)]")
$(info "run toolchain/edk2.sh setup first")
$(error "exiting")
endif

ifeq ($(call exists,Makefile.local),false)
$(shell cp toolchain/Makefile.template Makefile.local)
endif

# -------------- #
#   Toolchain    #
# -------------- #

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

NASM = nasm

CLANG = clang
CLANGXX = clang++
LLD_LINK = lld-link

GDB ?= gdb

QEMU ?= qemu-system-$(ARCH)

# ------------------ #
#  Compiler Options  #
# ------------------ #

CFLAGS += -std=gnu17 -Wall -m64 -ffreestanding -nostdlib \
		  -mcmodel=large -mno-red-zone -fno-stack-protector -MMD \
		  -fno-omit-frame-pointer -D__KERNEL__
CXXFLAGS += -std=gnu++17 -Wall -m64 -ffreestanding -nostdlib \
			-mcmodel=large -mno-red-zone -fno-stack-protector -MMD \
			-fno-omit-frame-pointer -fno-rtti -fno-exceptions
LDFLAGS += -Tlinker.ld -nostdlib -z max-page-size=0x1000
ASFLAGS +=
NASMFLAGS += -f elf64

INCLUDE += -Iinclude -Iinclude/kernel -Iinclude/fs -Ilib

ifeq ($(DEBUG),1)
CFLAGS += -gdwarf
CXXFLAGS += -gdwarf
ASFLAGS += -gdwarf-5
NASMFLAGS += -g -F dwarf
endif

# -------------- #
#  QEMU Options  #
# -------------- #

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

# HID Keyboard
ifeq ($(ATTACH_USB_KEYBOARD),1)
QEMU_OPTIONS += -device usb-kbd,bus=xhci.0
endif
# HID Mouse
ifeq ($(ATTACH_USB_MOUSE),1)
QEMU_OPTIONS += -device usb-mouse,bus=xhci.0
endif
# USB Mass Storage (SCSI Disk)
ifeq ($(ATTACH_USB_DISK),1)
USB_DISK_DRIVE ?= boot
QEMU_OPTIONS += -device usb-storage,drive=$(USB_DISK_DRIVE),bus=xhci.0
endif
