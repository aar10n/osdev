#
# sys makefile
#

INCLUDE-sys = -Ilibc -Ilib
CFLAGS-sys = -gdwarf -Wall -m64 -ffreestanding -nostdlib -MMD -fno-omit-frame-pointer
LDFLAGS-sys = -m elf_x86_64 -nostdlib

# sys
sys += hello.c ../libc/crt0.asm

# sys-y
sys-y = $(call objects,sys,$(BUILD_DIR)/sys)
