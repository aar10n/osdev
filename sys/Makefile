#
# sys makefile
#

SYS_ROOT = $(BUILD)/sysroot

CC-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc
LD-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc

CFLAGS-sys = -gdwarf -g3 -O0 -Wall
LDFLAGS-sys = -g3
INCLUDE-sys = -Iinclude/sys

# sys
sys += hello.c

# sys-y
sys-y = $(call objects,sys,$(BUILD_DIR)/sys)
