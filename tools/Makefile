NAME = tools
VERSION = 1.0

BUILD = ../build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  = -g -Wall -Wextra
INCLUDE =

include ../Makefile.util

# --------- #
#  Sources  #
# --------- #

ext2 = ext2/main.c

initrd = \
	initrd/main.c \
	initrd/create.c \
	initrd/fs.c \
	initrd/path.c \
	initrd/read.c \
	initrd/write.c

initrd-y = $(call objects,initrd,$(BUILD_DIR))

# --------- #
#  Targets  #
# --------- #

help:
	@echo "==============="
	@echo "    Targets    "
	@echo "==============="
	@echo "  ext2 	- Interact with ext2 file systems"
	@echo "  initrd - Generate ramdisk file systems"
	@echo $(initrd-y)

all: ext2 initrd

ext2: $(BUILD)/ext2
initrd: $(BUILD)/initrd

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

# -------------- #
#  Dependencies  #
# -------------- #

$(BUILD)/ext2: $(ext2)
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) $^ -o $@

$(BUILD)/initrd: $(initrd-y)
	$(CC) $^ -o $@

# Compilation rules

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(INCLUDE) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.s.o: %.s
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) $< -o $@
