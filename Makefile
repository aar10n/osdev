NAME = osdev
VERSION = 1.0
TARGET = i386

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  = -g -Wall -Wextra
LDFLAGS = -T linker.ld
ASFLAGS =

INCLUDE = -Iinclude -Ilibc

include util/Makefile.toolchain

# --------- #
#  Sources  #
# --------- #

kernel = \
	boot.s \
	kernel/main.c \
	kernel/cpu/asm.s \
	kernel/cpu/exception.s \
	kernel/cpu/interrupt.s \
	kernel/cpu/exception.c \
	kernel/cpu/gdt.c \
	kernel/cpu/idt.c \
	kernel/cpu/interrupt.c \
	kernel/cpu/pdt.c \
	kernel/cpu/rtc.c \
	kernel/cpu/timer.c \
	kernel/mem/heap.c \
	kernel/mem/mm.c \
	kernel/mem/page.c

kernel-y = $(patsubst %.c,$(BUILD_DIR)/%_c.o, \
	$(patsubst %.s,$(BUILD_DIR)/%_s.o,$(kernel)))


drivers = \
	drivers/ata.c \
	drivers/keyboard.c \
	drivers/rtc.c \
	drivers/screen.c \
	drivers/serial.c

drivers-y = $(patsubst %.c,$(BUILD_DIR)/%_c.o, \
	$(patsubst %.s,$(BUILD_DIR)/%_s.o,$(drivers)))


fs = \
	fs/fs.c \
	fs/super.c \
	fs/ext2/ext2.c \
	fs/ext2/dir.c \
	fs/ext2/inode.c \
	fs/ext2/super.c

fs-y = $(patsubst %.c,$(BUILD_DIR)/%_c.o, \
	$(patsubst %.s,$(BUILD_DIR)/%_s.o,$(fs)))


libc = \
	libc/math.c \
	libc/stdio.c \
	libc/stdlib.c \
	libc/string.c

libc-y = $(patsubst %.c,$(BUILD_DIR)/%_c.o, \
	$(patsubst %.s,$(BUILD_DIR)/%_s.o,$(libc)))

# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.iso;

run: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -serial file:$(BUILD)/stdio \
		-drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-drive file=$(BUILD)/osdev.iso,media=cdrom

debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S \
		-drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-drive file=$(BUILD)/osdev.iso,media=cdrom &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/osdev.bin"

run-debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S \
		-drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-drive file=$(BUILD)/osdev.iso,media=cdrom &

clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

# -------------- #
#  Dependencies  #
# -------------- #

$(BUILD)/osdev.iso: $(BUILD)/osdev.bin $(BUILD)/disk.img grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $< $(BUILD)/iso/boot/osdev
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	$(MKRESCUE) -o $@ $(BUILD)/iso &> /dev/null

$(BUILD)/osdev.bin: $(kernel-y) $(drivers-y) $(fs-y) $(libc-y)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD)/disk.img:
	@echo $(shell util/create-disk.sh $@)

# Compilation rules

$(BUILD_DIR)/%_c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_s.o: %.s
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
