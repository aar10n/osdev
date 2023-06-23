# PREREQUISITES:
#   - qemu
#	- clang
#	- lld-link
#	- nasm
#	- mtools
include scripts/utils.mk


# =========== Initial Setup =========== #
ifeq ($(call exists,.config),false)
.DEFAULT_GOAL := setup

.PHONY: setup
setup: ARCH = x86_64
setup: TOOLCHAIN = $(ARCH)-linux-musl
setup: BUILD_DIR = build
setup: TOOL_ROOT = $(BUILD_DIR)/toolchain
setup: SYS_ROOT = $(BUILD_DIR)/sysroot
setup:
	@echo "setting up project..."

	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(TOOL_ROOT)
	@mkdir -p $(SYS_ROOT)
	cp -n toolchain/Makefile.template Makefile.local || true
	cp -n toolchain/initrdrc.template .initrdrc || true
	git submodule update --init --recursive

	@echo
	@echo "ARCH:        $(ARCH)"
	@echo "TOOLCHAIN:   $(TOOLCHAIN)"
	@echo "PROJECT_DIR: $(shell pwd)"
	@echo "BUILD_DIR:   $(abspath $(BUILD_DIR))"
	@echo "TOOL_ROOT:   $(abspath $(TOOL_ROOT))"
	@echo "SYS_ROOT:    $(abspath $(SYS_ROOT))"
	@echo
	@printf "is this ok? (y/n) "
	@read -r answer; \
	if [ "$$answer" != "y" ]; then \
		echo "aborting."; \
		rm -f .config; \
		exit 1; \
	fi

#   write .config file
	$(file >  .config,ARCH := $(ARCH))
	$(file >> .config,TOOLCHAIN := $(TOOLCHAIN))
	$(file >> .config,PROJECT_DIR := $(shell pwd))
	$(file >> .config,BUILD_DIR := $(abspath $(BUILD_DIR)))
	$(file >> .config,TOOL_ROOT := $(abspath $(TOOL_ROOT)))
	$(file >> .config,SYS_ROOT := $(abspath $(SYS_ROOT)))

	@echo
	@echo "setup complete."
	@echo "run 'make -C toolchain all' to build the target toolchain. (this can take a while)"
	@echo "then run 'make all' to build a bootable image, and 'make run' to run it in qemu."

ifneq ($(subst setup,,$(MAKECMDGOALS)),)
# if not running `make` or `make setup`
$(error "Please run 'make setup' first.")
endif

else
# ===================================== #
.DEFAULT_GOAL := all
include .config

NAME := osdev
OBJ_DIR = $(BUILD_DIR)/$(NAME)
EDK_DIR = $(BUILD_DIR)/edk2

CFLAGS += -std=gnu17 -Wall -MMD -ffreestanding -nostdlib
CXXFLAGS += -std=gnu++17 -Wall -MMD -ffreestanding -nostdlib -fno-rtti -fno-exceptions
LDFLAGS +=
ASFLAGS +=
NASMFLAGS +=
INCLUDE += -Iinclude/

# arch-specific flags
ifeq ($(ARCH),x86_64)
CFLAGS += -m64 -masm=intel
CXXFLAGS += -m64
LDFLAGS += -m elf_x86_64
BOOT_NASMFLAGS += -f win64
KERNEL_NASMFLAGS += $(NASMFLAGS) -f elf64
else
$(error "Unsupported architecture: $(ARCH)")
endif

include Makefile.local
include scripts/defs.mk

ifeq ($(DEBUG),1)
CFLAGS += -gdwarf-5
CXXFLAGS += -gdwarf-5
LDFLAGS += -g
ASFLAGS += -gdwarf-5
NASMFLAGS += -g -F dwarf -O0
endif


# kernel + bootloader sources
MODULES = BOOT KERNEL
TARGETS = boot fs kernel lib # drivers
include $(foreach target,$(TARGETS),$(target)/Makefile)
$(call init-modules,$(MODULES))

# directories with userspace binaries
USERSPACE_DIRS = sbin

# =========== Build Rules =========== #

all: $(BUILD_DIR)/osdev.img

run: $(BUILD_DIR)/osdev.img
	$(QEMU) $(QEMU_OPTIONS) -monitor stdio

debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD_DIR)/kernel.elf"

run-debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) -monitor telnet:127.0.0.1:55544,server,nowait &> $(BUILD_DIR)/output &

remote-run: REMOTE_QEMU ?= $(QEMU)
remote-run: REMOTE_QEMU_OPTIONS ?= $(QEMU_OPTIONS)
remote-run: DEBUG_DIR = .
remote-run: $(BUILD_DIR)/osdev.img ovmf $(REMOTE_RUN_DEPS)
ifneq ($(call all-defined,REMOTE_USER REMOTE_HOST),true)
	$(error REMOTE_USER and REMOTE_HOST must both be defined)
endif
	$(RSYNC) -av $(BUILD_DIR)/osdev.img $(REMOTE_USER)@$(REMOTE_HOST):~/
	$(RSYNC) -av $(BUILD_DIR)/OVMF_$(WINARCH).fd $(REMOTE_USER)@$(REMOTE_HOST):~/
	$(SSH) -t $(REMOTE_USER)@$(REMOTE_HOST) "$(REMOTE_QEMU) $(REMOTE_QEMU_OPTIONS) -monitor stdio"

clean: clean-bootloader clean-kernel clean-userspace
	rm -f $(BUILD_DIR)/osdev.img
	rm -f $(BUILD_DIR)/initrd.img
	rm -f $(BUILD_DIR)/sysroot_sha1
	rm -f $(BUILD_DIR)/initrdrc
	rm -rf $(SYS_ROOT)


# efi bootable image
$(BUILD_DIR)/osdev.img: config.ini $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/initrd.img
	dd if=/dev/zero of=$@ bs=1M count=256
# 	256M -> 268435456 / 512 = 524288 sectors
	mformat -i $@ -F -h 64 -s 32 -T 524288 -c 1 -v osdev ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $^ ::/EFI/BOOT

# initrd filesystem image
initrd: $(BUILD_DIR)/initrd.img
$(BUILD_DIR)/initrd.img: .initrdrc $(BUILD_DIR)/initrdrc
	scripts/mkinitrd.py -o $@ $(foreach file,$^,-f $(file)) > /dev/null

#
# bootloader
#

bootloader: $(BUILD_DIR)/boot$(WINARCH).efi

clean-bootloader:
	rm -f $(BUILD_DIR)/boot$(WINARCH).efi
	rm -f $(BUILD_DIR)/loader{.dll,.lib}
	rm -rf $(OBJ_DIR)/boot

clean-bootloader-all: clean-bootloader
	rm -f $(BUILD_DIR)/static_library_files.lst
	rm -rf $(EDK_DIR)/Build/Loader

# bootloader efi application
$(BUILD_DIR)/boot$(WINARCH).efi: $(BUILD_DIR)/loader.dll
	$(EDK_DIR)/BaseTools/BinWrappers/PosixLike/GenFw -e UEFI_APPLICATION -o $@ $^

$(BUILD_DIR)/loader.dll: $(BUILD_DIR)/static_library_files.lst $(BOOT_OBJECTS)
	$(LLD_LINK) $(BOOT_LDFLAGS) /lldmap @$< /OUT:$@ $(BOOT_OBJECTS)

# edk2 library dependencies
$(BUILD_DIR)/static_library_files.lst: boot/LoaderPkg.dsc boot/Loader.inf
	EDK2_DIR=$(EDK_DIR) EDK2_BUILD_TYPE=$(EDK2_BUILD) bash toolchain/edk2.sh build $(WINARCH) loader-lst

#
# kernel
#

kernel: $(BUILD_DIR)/kernel.elf

clean-kernel:
	rm -f $(BUILD_DIR)/kernel.elf
	rm -rf $(OBJ_DIR)/{$(call join-comma,$(KERNEL_TARGETS))}

# loadable kernel elf
$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) $(BUILD_DIR)/libdwarf_kernel.a
	$(LD) $(call module-var,LDFLAGS,KERNEL) -o $@ --no-relax $^

# kernel libdwarf
$(BUILD_DIR)/libdwarf_kernel.a: $(TOOL_ROOT)/lib/libdwarf.a
	$(OBJCOPY) $^ $@ \
		--redefine-sym malloc=kmalloc \
		--redefine-sym calloc=kcalloc \
		--redefine-sym free=kfree \
		--redefine-sym printf=kprintf \
		--redefine-sym realloc=__debug_realloc_stub \
		--redefine-sym fclose=__debug_fclose_stub \
		--redefine-sym getcwd=__debug_getcwd_stub \
		--redefine-sym do_decompress_zlib=__debug_do_decompress_zlib_stub \
		--redefine-sym uncompress=__debug_uncompress_stub

$(TOOL_ROOT)/lib/libdwarf.a:
	$(MAKE) -C toolchain libdwarf

#
# userspace
#

userspace: $(USERSPACE_DIRS:%=userspace-dir-%)
install-userspace: $(USERSPACE_DIRS:%=userspace-dir-install-%)
clean-userspace: $(USERSPACE_DIRS:%=userspace-dir-clean-%)

userspace-dir-%:
	$(MAKE) -C $*

userspace-dir-install-%: sysroot
	$(MAKE) -C $* install
	@touch $(BUILD_DIR)/sysroot_sha1

userspace-dir-clean-%:
	$(MAKE) -C $* clean
	@touch $(BUILD_DIR)/sysroot_sha1

#
# sysroot
#

sysroot: $(SYS_ROOT)

.PHONY: $(SYS_ROOT)
$(SYS_ROOT): | install-userspace
	$(MAKE) -C toolchain musl-headers DESTDIR=$(SYS_ROOT)/usr
	@touch $(BUILD_DIR)/sysroot_sha1

# sysroot sha1sum
$(BUILD_DIR)/sysroot_sha1:
	echo "$$(tar -cf - $(SYS_ROOT) | sha1sum | awk '{print $$1}')" > $@

#
# external dependencies
#

ovmf: $(BUILD_DIR)/OVMF_$(WINARCH).fd
ext2_img: $(BUILD_DIR)/ext2.img

# sysroot initrd manifest
$(BUILD_DIR)/initrdrc: $(BUILD_DIR)/sysroot_sha1 | $(SYS_ROOT)
	scripts/gen_initrdrc.py $@ -S $(SYS_ROOT) -d $(SYS_ROOT):/

# edk2 ovmf firmware
$(BUILD_DIR)/OVMF_$(WINARCH).fd:
	@EDK2_DIR=$(EDK_DIR) EDK2_BUILD_TYPE=$(EDK2_BUILD) bash toolchain/edk2.sh build $(WINARCH) ovmf

# test ext2 filesystem
$(BUILD_DIR)/ext2.img: $(call pairs-src-paths, $(EXT2_DEPS))
	scripts/mkdisk.pl -o $@ -s 256M $(EXT2_DEPS)

#
# misc. targets
#

tail-logs:
	tail -f $(BUILD_DIR)/kernel.log

remote-tail-logs:
	$(SSH) -t $(REMOTE_USER)@$(REMOTE_HOST) "tail -f kernel.log"

copy-to-pxe-server: $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf configpxe.ini
ifneq ($(call all-defined,PXE_USER PXE_HOST PXE_ROOT),true)
	$(error PXE_USER, PXE_HOST and PXE_ROOT must all be defined)
endif
	$(RSYNC) -av $(BUILD_DIR)/{boot$(WINARCH).efi,kernel.elf} $(PXE_USER)@$(PXE_HOST):$(PXE_ROOT)/
	$(RSYNC) -av configpxe.ini $(PXE_USER)@$(PXE_HOST):$(PXE_ROOT)/config.ini

copy-to-usb: $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf config.ini
ifneq ($(call all-defined,USB_DEVICE),true)
	$(error USB_DEVICE must be defined)
endif
	rm -rf $(USB_DEVICE)/EFI
	mkdir -p $(USB_DEVICE)/EFI/BOOT
	cp $(BUILD_DIR)/boot$(WINARCH).efi $(USB_DEVICE)/EFI/BOOT/boot$(WINARCH).EFI
	cp $(BUILD_DIR)/kernel.elf $(USB_DEVICE)/EFI/BOOT/kernel.elf
	cp config.ini $(USB_DEVICE)/EFI/BOOT/config.ini

print-debug-var:
ifndef VAR
	$(error VAR is undefined)
endif
	@echo "VAR: $(VAR)"
ifdef MODULE
	@echo "MODULE: $(MODULE)"
	@echo "value: $(call module-var,$(VAR),$(MODULE))"
else ifdef FILE
	@echo "FILE: $(FILE)"
	@echo "value: $(call var,$(VAR),$(FILE))"
else
	$(error "MODULE or FILE must be defined")
endif

# ------------------- #
#  Compilation Rules  #
# ------------------- #

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/%.c
	@mkdir -p $(@D)
	$(call var,CC,$<) $(call var,INCLUDE,$<) $(call var,CFLAGS,$<) $(call var,DEFINES,$<) -o $@ -c $<

$(OBJ_DIR)/%.cpp.o: $(PROJECT_DIR)/%.cpp
	@mkdir -p $(@D)
	$(call var,CXX,$<) $(call var,INCLUDE,$<) $(call var,CXXFLAGS,$<) $(call var,DEFINES,$<) -o $@ -c $<

$(OBJ_DIR)/%.s.o: $(PROJECT_DIR)/%.s
	@mkdir -p $(@D)
	$(call var,AS,$<) $(call var,INCLUDE,$<) $(call var,ASFLAGS,$<) -o $@ $<

$(OBJ_DIR)/%.asm.o: $(PROJECT_DIR)/%.asm
	@mkdir -p $(@D)
	$(call var,NASM,$<) $(call var,INCLUDE,$<) $(call var,NASMFLAGS,$<) -o $@ $<

-include $(call include-module-deps,$(MODULES))

endif
