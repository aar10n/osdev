NAME = osdev
VERSION = 1.0
TARGET = i686

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  += -g
LDFLAGS +=
ASFLAGS +=
NASMFLAGS += -g

INCLUDE = -Iinclude -Ilib -Ilibc

QEMUFLAGS = -usb \
			-vga std \
			-cpu Nehalem \
			-smp cores=1,threads=2 \
			-m 256M \
			-rtc base=localtime,clock=host \
			-serial file:$(BUILD)/stdio \
			-drive file=$(BUILD)/disk.img,format=raw,if=ide \
			-drive file=$(BUILD)/disk.img,format=raw,if=none,id=disk \
			-device ahci,id=ahci \
			-device ide-hd,drive=disk,bus=ahci.0 \
			-drive file=$(BUILD)/osdev.iso,media=cdrom

include scripts/Makefile.toolchain

# --------- #
#  Targets  #
# --------- #

targets = kernel drivers fs lib libc

# include the makefiles of all targets
include $(foreach t, $(targets), $(t)/Makefile)

# generate a list of all source objects by applying the
# `objects` function to each list of target sources
obj-y = $(foreach t, $(targets), \
			$(call objects, $($(t)), $(BUILD_DIR)/$(t)))

# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.iso

run: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) $(QEMUFLAGS)

run-bochs: $(BUILD)/osdev.iso $(BUILD)/disk.img
	bochs

debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S $(QEMUFLAGS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/osdev.bin"

debug-real: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S $(QEMUFLAGS) &
	$(GDB) -w \
		--command scripts/gdb_real_mode.txt \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/osdev.bin"

run-debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	$(QEMU) -s -S $(QEMUFLAGS) &

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)
	$(MAKE) -C tools clean

# Tools

.PHONY: tools
tools:
	$(MAKE) -C tools all

.PHONY: initrd
initrd:
	$(MAKE) -C tools initrd

ramdisk: initrd $(BUILD)/initrd.img

# -------------- #
#  Dependencies  #
# -------------- #

# Kernel

$(BUILD)/osdev.iso: $(BUILD)/osdev.bin $(BUILD)/initrd.img grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	mkdir -p $(BUILD)/iso/modules
	cp $(BUILD)/osdev.bin $(BUILD)/iso/boot/osdev
	cp $(BUILD)/initrd.img $(BUILD)/iso/modules/initrd
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	$(MKRESCUE) -o $@ $(BUILD)/iso &> /dev/null

$(BUILD)/osdev.bin: $(kernel-y) $(drivers-y) $(fs-y) $(lib-y) $(libc-y) $(obj-y)
	$(LD) $(LDFLAGS) $^ -o $@

# External Data

$(BUILD)/disk.img:
	scripts/create-disk.sh $@

$(BUILD)/initrd.img: $(BUILD)/initrd
	scripts/create-initrd.sh $< $@


# ------------------- #
#  Compilation Rules  #
# ------------------- #

$(BUILD_DIR)/%_c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(INCLUDE) $(CFLAGS) $(CFLAGS-$<) -c $< -o $@

$(BUILD_DIR)/%_cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(INCLUDE) $(CXXFLAGS) $(CXXFLAGS-$<) -c $< -o $@

$(BUILD_DIR)/%_s.o: %.s
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $(ASFLAGS-$<) $< -o $@

$(BUILD_DIR)/%_asm.o: %.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) $(NASMFLAGS-$<) $< -o $@
