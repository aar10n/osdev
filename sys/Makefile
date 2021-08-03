#
# sys makefile
#

SYS_ROOT = $(BUILD)/sysroot

CC-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc
LD-sys = $(SYS_ROOT)/bin/x86_64-osdev-ld

CFLAGS-sys = -gdwarf -Wall
LDFLAGS-sys = -lc
INCLUDE-sys = -I

# sys
sys += hello.c

# sys-y
sys-y = $(call objects,sys,$(BUILD_DIR)/sys)
