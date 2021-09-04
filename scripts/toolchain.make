#
# Compiler toolchain options
#

TOOLCHAIN = x86_64-elf-

#QEMU = qemu-system-x86_64
QEMU = /Users/Aaron/Projects/qemu/build/qemu-system-x86_64

CXX_STD = -std=c++20
C_STD = -std=c17

CC = $(TOOLCHAIN)gcc
CXX = $(TOOLCHAIN)g++
LD = $(TOOLCHAIN)ld
AR = $(TOOLCHAIN)ar
AS = $(TOOLCHAIN)as
NASM = nasm

STRIP = $(TOOLCHAIN)strip
READELF = $(TOOLCHAIN)readelf
OBJCOPY = $(TOOLCHAIN)objcopy

GDB = $(TOOLCHAIN)gdb

CFLAGS += -gdwarf -Wall -m64 -ffreestanding -nostdlib \
	-mcmodel=large -mno-red-zone -fno-stack-protector -MMD \
	-fno-omit-frame-pointer
CXXFLAGS += $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS += -gdwarf-5
NASMFLAGS += -f elf64 -g -F dwarf
LDFLAGS += -m elf_x86_64 -Tlinker.ld -nostdlib -z max-page-size=0x1000
QEMUFLAGS +=


ifndef TARGET
	TARGET = x86_64
endif

# Target
ifeq ($(TARGET),x86_64)
	CFLAGS +=
	CXXFLAGS +=
	ASFLAGS +=
	NASMFLAGS +=
	LDFLAGS +=
else
    $(error unknown target $(TARGET))
endif