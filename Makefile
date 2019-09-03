NAME = osdev
VERSION = 1.0
TARGET = i386

BUILD = build

CFLAGS  = -g -Wall -Wextra
ASFLAGS =
LDFLAGS =

INCLUDE = -Iinclude -Ilibc

include util/Makefile.toolchain

# -------------- #
#  Dependencies  #
# -------------- #

drivers = \
	drivers/asm.asm \
	drivers/ata.c \
	drivers/keyboard.c \
	drivers/rtc.c \
	drivers/screen.c \
	drivers/serial.c

fs = \
	fs/fs.c \
	fs/ext2/ext2.c \
	fs/ext2/dir.c \
	fs/ext2/inode.c \
	fs/ext2/super.c

libc = \
	libc/math.c \
	libc/stdio.c \
	libc/stdlib.c \
	libc/string.c

kernel = \
	kernel/main.c \
	kernel/cpu/interrupt.asm \
	kernel/cpu/gdt.c \
	kernel/cpu/idt.c \
	kernel/cpu/isr.c \
	kernel/cpu/pdt.c \
	kernel/cpu/timer.c \
	kernel/mem/heap.c \
	kernel/mem/mm.c \
	kernel/mem/page.c

SOURCES = $(kernel) $(drivers) $(fs) $(libc)
OBJECTS = build/boot.o \
	$(filter %.o,$(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) \
	$(patsubst %.asm,$(BUILD)/%.o,$(SOURCES)))

# --------- #
#  Targets  #
# --------- #


all: $(BUILD)/osdev.iso $(BUILD)/disk.img

run: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -serial file:$(BUILD)/stdio					\
		-drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-drive file=$(BUILD)/osdev.iso,media=cdrom	    \

debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S 										\
    	-drive file=$(BUILD)/disk.img,format=raw,if=ide \
        -drive file=$(BUILD)/osdev.iso,media=cdrom	    &
	$(GDB) -w 											\
		-ex "target remote localhost:1234"				\
		-ex "add-symbol $(BUILD)/kernel.bin"

clean:
	rm -rf $(BUILD)
	mkdir $(BUILD)

#
#
#

# osdev.iso - Bootable iso
$(BUILD)/osdev.iso: $(BUILD)/kernel.bin grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $< $(BUILD)/iso/boot/osdev
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	$(MKRESCUE) -o $@ $(BUILD)/iso &> /dev/null

# kernel - Kernel binary
$(BUILD)/kernel.bin: $(OBJECTS)
	$(LD) -T linker.ld $^ -o $@

$(BUILD)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
