NAME = osdev
VERSION = 1.0
TARGET = x86_64

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  += -g
LDFLAGS +=
ASFLAGS +=
NASMFLAGS += -g

INCLUDE = -Iinclude -Ilib -Ilibc \
	-Ithird-party/gnu-efi/inc \
	-Ithird-party/gnu-efi/inc/x86_64 \
	-Ithird-party/gnu-efi/inc/protocol

#QEMUFLAGS = -usb \
#			-vga std \
#			-cpu Nehalem \
#			-smp cores=1,threads=2 \
#			-m 256M \
#			-rtc base=localtime,clock=host \
#			-serial file:$(BUILD)/stdio \
#			-drive file=$(BUILD)/disk.img,format=raw,if=ide \
#			-drive file=$(BUILD)/disk.img,format=raw,if=none,id=disk \
#			-device ahci,id=ahci \
#			-device ide-hd,drive=disk,bus=ahci.0 \
#			-drive file=$(BUILD)/osdev.iso,media=cdrom

QEMUFLAGS = -bios scripts/OVMF-DEBUG.fd -usb \
	-drive file=$(BUILD)/osdev.img,id=boot,format=raw,if=none \
	-device usb-storage,drive=boot


include scripts/Makefile.toolchain

# --------- #
#  Targets  #
# --------- #

targets = boot kernel drivers lib libc

# include the makefiles of all targets
include $(foreach t,$(targets),$t/Makefile)

# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.img

run: $(BUILD)/osdev.img
	$(QEMU) $(QEMUFLAGS)

debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/osdev.bin"

run-debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) &

.PHONY: clean
clean:
	rm -f $(BUILD)/*.img
	rm -f $(BUILD)/*.efi
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

# USB bootable image
$(BUILD)/osdev.img: $(BUILD)/bootx64.efi
	dd if=/dev/zero of=$@ bs=1k count=1440
	mformat -i $@ -f 1440 ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/boot
	mmd -i $@ ::/EFI/osdev
	mcopy -i $@ $(BUILD)/bootx64.efi ::/EFI/boot

# EFI boot code
$(BUILD)/bootx64.efi: $(boot-y) # $(libc-y) $(lib-y)
	$(LD) $(call flags,$<,LDFLAGS) $^ -o $@

# Kernel code
$(BUILD)/kernel.elf: $(kernel-y) # $(drivers-y) $(libc-y) $(lib-y)
	$(LD) $(call flags,$<,LDFLAGS) $^ -o $@

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
	$(CC) $(INCLUDE) $(call flags,$<,CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(INCLUDE) $(call flags,$<,CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%_s.o: %.s
	@mkdir -p $(@D)
	$(AS) $(call flags,$<,ASFLAGS) $< -o $@

$(BUILD_DIR)/%_asm.o: %.asm
	@mkdir -p $(@D)
	$(NASM) $(call flags,$<,NASMFLAGS) $< -o $@


-include *.d
