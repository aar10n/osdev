NAME = osdev
VERSION = 1.0
TARGET = x86_64

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  = -g -Wall -Wextra
LDFLAGS = -T linker.ld
ASFLAGS =

INCLUDE = -Iinclude -Ilibc

QEMU_FLAGS = -usb \
             -vga std \
             -m 256M \
             -serial file:$(BUILD)/stdio \
             -drive file=$(BUILD)/disk.img,format=raw,if=ide \
             -drive file=$(BUILD)/osdev.iso,media=cdrom

include util/Makefile.toolchain

# --------- #
#  Sources  #
# --------- #

# Generates a list of objects from a list of sources
objects = $(patsubst %.c,$(BUILD_DIR)/%_c.o, \
		  $(patsubst %.s,$(BUILD_DIR)/%_s.o, \
		  $(patsubst %.asm,$(BUILD_DIR)/%_asm.o, \
		  $(1))))

kernel = \
	boot.asm \
	kernel/main.c \
	kernel/cpu/asm.asm \
	kernel/cpu/exception.asm \
	kernel/cpu/interrupt.asm \
	kernel/cpu/exception.c \
	kernel/cpu/gdt.c \
	kernel/cpu/idt.c \
	kernel/cpu/interrupt.c \
	kernel/cpu/pdt.c \
	kernel/cpu/rtc.c \
	kernel/cpu/timer.c \
	kernel/mem/heap.c \
	kernel/mem/mm.c \
	kernel/mem/paging.c \
	kernel/vga/vga.c \

kernel-y := $(call objects,$(kernel))


drivers = \
	drivers/ata.c \
	drivers/keyboard.c \
	drivers/rtc.c \
	drivers/screen.c \
	drivers/serial.c

drivers-y = $(call objects,$(drivers))


fs = \
	fs/fs.c \
	fs/super.c \
	fs/ext2/ext2.c \
	fs/ext2/dir.c \
	fs/ext2/inode.c \
	fs/ext2/super.c

fs-y = $(call objects,$(fs))


libc = \
	libc/builtins/divdi3.s \
	libc/builtins/udivdi3.s \
	libc/math/math.c \
	libc/stdio/printf.c \
	libc/stdio/printf_fp.c \
	libc/stdio/stdio.c \
	libc/stdlib/stdlib.c \
	libc/string/string.c

libc-y = $(call objects,$(libc))

# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.iso;

run: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) $(QEMU_FLAGS)

debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S $(QEMU_FLAGS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/osdev.bin"

run-debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S $(QEMU_FLAGS) &

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

# Other targets

tools:
	cd tools && $(MAKE) all

.PHONY: test
test:
	$(info $(libc-y))

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

$(BUILD_DIR)/%_asm.o: %.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) $< -o $@
