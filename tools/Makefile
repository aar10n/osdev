NAME = tools
VERSION = 1.0
TARGET = default

BUILD = ../build/tools

CFLAGS  = -Wall -Wextra
INCLUDE =

include ../scripts/Makefile.toolchain

# --------- #
#  Sources  #
# --------- #

ext2 = ext2/main.c
initrd = initrd/main.c

# --------- #
#  Targets  #
# --------- #

help:
	@echo "==============="
	@echo "    Targets    "
	@echo "==============="
	@echo "  ext2 	- Interact with ext2 file systems"
	@echo "  initrd - Generate ramdisk file systems"

all: ext2 initrd

ext2: $(BUILD)/ext2
initrd: $(BUILD)/initrd

# -------------- #
#  Dependencies  #
# -------------- #

$(BUILD)/ext2: $(ext2)
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) $^ -o $@

$(BUILD)/initrd: $(initrd)
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) $^ -o $@

# Compilation rules
