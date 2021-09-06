NAME = osdev
VERSION = 1.0
TARGET = x86_64

BUILD = build
BUILD_DIR = $(BUILD)/$(NAME)
SYS_ROOT = $(BUILD)/sysroot

CFLAGS  += -std=gnu17 -D__KERNEL__
LDFLAGS +=
ASFLAGS +=
NASMFLAGS += -g

INCLUDE = -Iinclude -Iinclude/kernel -Iinclude/fs -Ilib

QEMUFLAGS = \
	-cpu Nehalem \
	-smp cores=1,threads=2,sockets=1 \
	-machine q35 \
	-m 256M \
	-bios scripts/OVMF-DEBUG.fd \
	-global isa-debugcon.iobase=0x402 \
	-debugcon file:$(BUILD)/uefi_debug.log \
	-serial file:$(BUILD)/stdio \
	-drive file=$(BUILD)/osdev.img,id=boot,format=raw,if=none \
	-drive file=$(BUILD)/ext2.img,id=stick,format=raw,if=none \
	-device ahci,id=ahci \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-storage,drive=boot,bus=xhci.0 \
	-device usb-storage,drive=stick,bus=xhci.0 \
	-trace events=$(BUILD)/traces

include scripts/toolchain.make
include scripts/utils.make

# --------- #
#  Targets  #
# --------- #

targets = boot kernel fs drivers lib sys

# include the makefiles of all targets
include $(foreach t,$(targets),$t/Makefile)


# --------- #
#  Targets  #
# --------- #

all: $(BUILD)/osdev.img sys-all tools

run: $(BUILD)/osdev.img
	$(QEMU) $(QEMUFLAGS) -monitor stdio

debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD)/kernel.elf"

run-debug: $(BUILD)/osdev.img
	$(QEMU) -s -S $(QEMUFLAGS) -monitor telnet:127.0.0.1:55544,server,nowait &> $(BUILD)/output &

.PHONY: clean
clean:
	rm -f $(BUILD)/*.efi
	rm -f $(BUILD)/*.elf
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

.PHONY: clean-kernel
clean-kernel:
	rm -f $(BUILD)/*.efi
	rm -f $(BUILD)/*.elf
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

.PHONY: clean-sys
clean-sys:
	rm -rf $(BUILD_DIR)/sys
	mkdir $(BUILD_DIR)/sys
	rm -rf $(BUILD)/apps
	rm -rf $(BUILD)/libs
	mkdir $(BUILD)/apps
	mkdir $(BUILD)/libs

.PHONY: clean-all
clean-all:
	rm -f $(BUILD)/*.img
	rm -f $(BUILD)/*.efi
	rm -f $(BUILD)/*.elf
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)
	$(MAKE) -C tools clean

# -------------- #
#  Dependencies  #
# -------------- #

# USB bootable image
$(BUILD)/osdev.img: $(BUILD)/bootx64.efi $(BUILD)/kernel.elf config.ini $(BUILD)/ext2.img | sys-install
	dd if=/dev/zero of=$@ bs=1k count=1440
	mformat -i $@ -f 1440 -v osdev ::
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
$(BUILD)/kernel.elf: $(kernel-y) $(fs-y) $(drivers-y) $(lib-y)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -o $@

.PHONY: sys-all
sys-all: $(sys-targets)

.PHONY: sys-install
sys-install: $(BUILD)/ext2.img $(sys-install-targets)


# External Data

$(BUILD)/ext2.img: config.ini
	@mkdir -p $(SYS_ROOT)/usr/share/fonts/truetype
	cp $(BUILD)/arial.ttf $(SYS_ROOT)/usr/share/fonts/truetype
	cp $(BUILD)/routed-gothic.ttf $(SYS_ROOT)/usr/share/fonts/truetype
	dd if=/dev/zero of=$@ bs=1m count=512
	mke2fs -L Untitled -t ext2 $@
	cd $(SYS_ROOT) && find . -type f ! -name ".DS_Store" -exec e2cp {} ../../$@:/{} \;
	e2mkdir $@ /usr/bin
	-e2rm -r $@:/lost+found
	cp $(SYS_ROOT)/lib/ld.so $(BUILD)/ld.so
	cp $(SYS_ROOT)/lib/libc.so $(BUILD)/libc.so

# ------------------- #
#  Compilation Rules  #
# ------------------- #

$(BUILD)/apps/%: $(BUILD)/ext2.img
	e2cp $@ $<:/usr/bin/$(notdir $@)

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(call toolchain,$<,CC) $(call flags,$<,INCLUDE) $(call flags,$<,CFLAGS) -o $@ -c $<

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(call toolchain,$<,CXX) $(call flags,$<,INCLUDE) $(call flags,$<,CXXFLAGS) -o $@ -c $<

$(BUILD_DIR)/%.s.o: %.s
	@mkdir -p $(@D)
	$(call toolchain,$<,AS) $(call flags,$<,INCLUDE) $(call flags,$<,ASFLAGS) $< -o $@

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(call toolchain,$<,NASM) $(call flags,$<,INCLUDE) $(call flags,$<,NASMFLAGS) $< -o $@
