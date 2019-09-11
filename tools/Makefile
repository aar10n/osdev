NAME = tools
VERSION = 1.0
TARGET = default

BUILD = ../build

CFLAGS  = -Wall -Wextra
INCLUDE = -I../include

include ../util/Makefile.toolchain

# --------- #
#  Sources  #
# --------- #

ext2 = ext2/main.c

ext2-y = $(patsubst %.c,$(BUILD_DIR)/ext2/%.o,$(ext2))

# --------- #
#  Targets  #
# --------- #

help:
	@echo "==============="
	@echo "    Targets    "
	@echo "==============="
	@echo "  ext2 - A tool for interacting with ext2 filesystems"

all: $(BUILD)/ext2;

ext2: $(BUILD)/ext2;

# -------------- #
#  Dependencies  #
# -------------- #

$(BUILD)/ext2: $(ext2)
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) $^ -o $@

# Compilation rules
