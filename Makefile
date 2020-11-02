NAME = osdev
VERSION = 1.0
TARGET = x86_64

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)

CFLAGS  +=
LDFLAGS +=
ASFLAGS +=
NASMFLAGS += -g

INCLUDE = -Iinclude -Iinclude/kernel -Iinclude/fs -Iinclude/libc -Ilib

QEMUFLAGS = \
	-cpu Nehalem \
	-smp cores=1,threads=2,sockets=1 \
	-machine q35 \
	-m 256M \
	-no-reboot \
   	-no-shutdown \
	-bios scripts/OVMF-DEBUG.fd \
	-global isa-debugcon.iobase=0x402 \
	-debugcon file:$(BUILD)/uefi_debug.log \
	-serial file:$(BUILD)/stdio \
	-drive file=$(BUILD)/osdev.img,id=boot,format=raw,if=none \
	-drive file=$(BUILD)/osdev.img,id=disk,format=raw,if=none \
	-device ahci,id=ahci \
	-device usb-storage,drive=boot \
	-device ide-hd,drive=disk,bus=ahci.0 \
	-usb

include scripts/Makefile.toolchain
include scripts/Makefile.util

# --------- #
#  Targets  #
# --------- #

targets = boot kernel fs drivers libc lib

# include the makefiles of all targets
include $(foreach t,$(targets),$t/Makefile)


# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.img tools

run: $(BUILD)/osdev.img
	$(QEMU) $(QEMUFLAGS) -monitor stdio

debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/kernel.elf"

run-debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) -monitor telnet:127.0.0.1:55555,server,nowait &

.PHONY: clean
clean:
	rm -f $(BUILD)/*.img
	rm -f $(BUILD)/*.efi
	rm -f $(BUILD)/*.elf
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
$(BUILD)/osdev.img: $(BUILD)/bootx64.efi $(BUILD)/kernel.elf config.ini
	dd if=/dev/zero of=$@ bs=1k count=1440
	mformat -i $@ -f 1440 ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/boot
	mcopy -i $@ $(BUILD)/bootx64.efi ::/EFI/boot
	mcopy -i $@ config.ini ::/EFI/boot
	mcopy -i $@ $(BUILD)/kernel.elf ::/EFI

# EFI Bootloader
$(BUILD)/bootx64.efi: $(BUILD)/loader.dll
	cd third-party/edk2 && source edksetup.sh && \
	cd ../../ && GenFw -e UEFI_APPLICATION -o $@ $^

$(BUILD)/loader.dll: $(boot-y)
	lld-link /OUT:$@ /NOLOGO /NODEFAULTLIB /ALIGN:32 /FILEALIGN:32 \
		/SECTION:.xdata,D /SECTION:.pdata,D /Machine:X64 /ENTRY:_ModuleEntryPoint \
		/SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER /DLL /MERGE:.rdata=.data \
		/debug /lldmap @scripts/loader-libs.lst $^

# Kernel
$(BUILD)/kernel.elf: $(kernel-y) $(fs-y) $(libc-y) $(drivers-y) $(lib-y)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -o $@


# External Data

$(BUILD)/disk.img:
	scripts/create-disk.sh $@

$(BUILD)/initrd.img: $(BUILD)/initrd
	scripts/create-initrd.sh $< $@

# ------------------- #
#  Compilation Rules  #
# ------------------- #

include $(wildcard *.d)

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(call toolchain,$<,CC) $(call flags,$<,INCLUDE) $(call flags,$<,CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(call toolchain,$<,CXX) $(call flags,$<,INCLUDE) $(call flags,$<,CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.s.o: %.s
	@mkdir -p $(@D)
	$(call toolchain,$<,AS) $(call flags,$<,INCLUDE) $(call flags,$<,ASFLAGS) $< -o $@

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(call toolchain,$<,NASM) $(call flags,$<,INCLUDE) $(call flags,$<,NASMFLAGS) $< -o $@
