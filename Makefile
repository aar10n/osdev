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
setup: export ARCH = x86_64
setup: export BUILD_DIR = build
setup:
	@echo "setting up project..."
	./configure
	cp -n toolchain/Makefile.template Makefile.local || true
	cp -n toolchain/initrdrc.template .initrdrc || true
	git submodule update --init --recursive
	@echo
	@echo "setup complete."
	@echo "run 'make -C toolchain all' to build the target toolchain. (this can take a while)"
	@echo "then run 'make all' to build a bootable image, and 'make run' to run it in qemu."
	@echo "configure additional options in Makefile.local."

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

CFLAGS += -std=gnu17 -Wall -MMD -ffreestanding -nostdlib -Werror=int-conversion
CXXFLAGS += -std=gnu++17 -Wall -MMD -ffreestanding -nostdlib -fno-rtti -fno-exceptions
LDFLAGS +=
ASFLAGS +=
NASMFLAGS +=
INCLUDE += -I$(PROJECT_DIR)/ -Iinclude/ -Iinclude/uapi/

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

include scripts/defs.mk
include Makefile.local

ifeq ($(DEBUG),y)
CFLAGS += -gdwarf-5
CXXFLAGS += -gdwarf-5
LDFLAGS += -g
ASFLAGS += -gdwarf-5
NASMFLAGS += -g -F dwarf -O0
endif

# QEMU options

ifeq ($(QEMU_DEBUG),y)
QEMU_DEBUG_OPTIONS += \
	-global isa-debugcon.iobase=0xe9 \
	-debugcon file:$(DEBUG_DIR)/debugcon.log

ifdef QEMU_TRACE_FILE
QEMU_DEBUG_OPTIONS += -trace events=$(QEMU_TRACE_FILE),file=$(BUILD_DIR)/events.out
endif
endif

QEMU_OPTIONS ?= \
	-cpu $(QEMU_CPU) \
	-smp $(QEMU_SMP) \
	-m $(QEMU_MEM) \
	-machine $(QEMU_MACHINE) \
	-bios $(BUILD_DIR)/OVMF_$(WINARCH).fd \
	-drive file=$(BUILD_DIR)/osdev.img,id=boot,format=raw,if=none \
	-no-shutdown -no-reboot -action panic=pause \
	$(QEMU_DEVICES) \
	$(QEMU_SERIAL_DEVICES) \
	$(QEMU_DEBUG_OPTIONS) \
	$(QEMU_EXTRA_OPTIONS)


# kernel + bootloader sources
MODULES = BOOT KERNEL
TARGETS = boot fs kernel lib drivers
include $(foreach target,$(TARGETS),$(target)/Makefile)
$(call init-modules,$(MODULES))

# directories with userspace binaries
USERSPACE_DIRS = sbin bin usr.bin

# =========== Build Rules =========== #

all: $(BUILD_DIR)/osdev.img tools

run: QEMU_SHELL_DEVICE = telnet:127.0.0.1:8008,server,nowait
run: $(BUILD_DIR)/osdev.img
	$(QEMU) $(QEMU_OPTIONS) $(if $(QEMU_GDB),-s,) -monitor $(QEMU_MONITOR) $(if $(QEMU_NOGRAPHICS),-nographic,) $(EXTRA_OPTIONS)

run-shell: QEMU_SHELL_DEVICE = mon:stdio
run-shell: $(BUILD_DIR)/osdev.img
	$(QEMU) $(QEMU_OPTIONS) $(EXTRA_OPTIONS) -s -nographic

debug: QEMU_SHELL_DEVICE = mon:stdio
debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol-file $(BUILD_DIR)/kernel.elf"

run-debug: QEMU_SHELL_DEVICE = mon:stdio
run-debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) -monitor $(QEMU_MONITOR) 2>&1 > $(BUILD_DIR)/qemu.log &

ifeq ($(QEMU_BUILD_PLUGIN),y)
# if building the qemu profiling plugin add profiling targets

PROFILE_SYMBOL_FILES = \
	$(TOOL_ROOT)/usr/lib/libc.so@0x7fc0000000 \
	$(BUILD_DIR)/sbin/init/init@0x400000 \
	$(BUILD_DIR)/sbin/getty/getty@0x800000 \
	$(BUILD_DIR)/sbin/shell/shell@0xC00000 \
	$(BUILD_DIR)/bin/busybox/busybox@0x1000000 \
	$(BUILD_DIR)/usr.bin/doom/doom@0x1400000


run-profile: PERIOD = 10000
run-profile: VCPUS =
run-profile: OPTIONS = period=$(PERIOD),vcpus=$(VCPUS),$(EXTRA_OPTIONS)
run-profile: PROFILE = $(BUILD_DIR)/profile.folded
run-profile: $(BUILD_DIR)/osdev.img qemu-profile-plugin
	$(QEMU) $(QEMU_OPTIONS) -monitor $(QEMU_MONITOR) \
		-plugin $(QEMU_PROFILE_PLUGIN),output=$(PROFILE),$(OPTIONS) 2>&1 > $(BUILD_DIR)/qemu.log

profile-resolve: PROFILE = $(BUILD_DIR)/profile.folded
profile-resolve: $(PROFILE) $(BUILD_DIR)/osdev.syms
	@echo "Resolving profile symbols..."
	python ./scripts/gen_syms.py -k $(BUILD_DIR)/kernel.elf $(OPTIONS) $(foreach f,$(PROFILE_SYMBOL_FILES),-p $(f)) -o $(BUILD_DIR)/osdev.syms
	python ./scripts/resolve_profile.py $(PROFILE) $(BUILD_DIR)/osdev.syms > $(PROFILE).resolved

endif

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
.PHONY: osdev.img
osdev.img: $(BUILD_DIR)/osdev.img
$(BUILD_DIR)/osdev.img: config.ini $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/initrd.img
	dd if=/dev/zero of=$@ bs=1M count=256
# 	256M -> 268435456 / 512 = 524288 sectors
	mformat -i $@ -F -h 64 -s 32 -T 524288 -c 1 -v osdev :: # format as FAT32
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $^ ::/EFI/BOOT

# initrd filesystem image
initrd: $(BUILD_DIR)/initrd.img
$(BUILD_DIR)/initrd.img: .initrdrc $(BUILD_DIR)/initrdrc
	scripts/mkinitrd.py -o $@ $(foreach file,$^,-f $(file))

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
# linux headers
#

# Using prebuilt headers from Alpine Linux instead of building from source
ALPINE_VERSION = 3.20
LINUX_HEADERS_VERSION = 6.6-r0

linux-headers: $(BUILD_DIR)/linux-headers
	rm -f $(PROJECT_DIR)/include/uapi/linux
	rm -f $(PROJECT_DIR)/include/uapi/asm
	rm -f $(PROJECT_DIR)/include/uapi/asm-generic
	ln -sf $(BUILD_DIR)/linux-headers/linux $(PROJECT_DIR)/include/uapi/linux
	ln -sf $(BUILD_DIR)/linux-headers/asm $(PROJECT_DIR)/include/uapi/asm
	ln -sf $(BUILD_DIR)/linux-headers/asm-generic $(PROJECT_DIR)/include/uapi/asm-generic

# Download and extract prebuilt Linux headers from Alpine Linux
$(BUILD_DIR)/linux-headers: $(BUILD_DIR)/linux-headers-$(LINUX_HEADERS_VERSION).apk
	@echo "Extracting prebuilt Linux headers..."
	@rm -rf $@
	@mkdir -p $@
	@tar -xzf $< -C $@ 2>/dev/null || true
	@# Move headers to the expected structure
	@if [ -d "$@/usr/include/linux" ]; then \
		mv $@/usr/include/linux $@/linux; \
	fi
	@if [ -d "$@/usr/include/asm" ]; then \
		mv $@/usr/include/asm $@/asm; \
	fi
	@if [ -d "$@/usr/include/asm-generic" ]; then \
		mv $@/usr/include/asm-generic $@/asm-generic; \
	fi
	@# Clean up extra directories
	@rm -rf $@/usr $@/.PKGINFO $@/.SIGN.*
	@echo "Linux headers extracted successfully"

.PRECIOUS: $(BUILD_DIR)/linux-headers-$(LINUX_HEADERS_VERSION).apk
$(BUILD_DIR)/linux-headers-$(LINUX_HEADERS_VERSION).apk:
	@mkdir -p $(BUILD_DIR)
	@echo "Downloading prebuilt Linux headers package..."
	wget https://dl-cdn.alpinelinux.org/alpine/v$(ALPINE_VERSION)/main/$(ARCH)/linux-headers-$(LINUX_HEADERS_VERSION).apk -O $@


#
# userspace
#

userspace: $(USERSPACE_DIRS:%=userspace-dir-%)
install-userspace: install-etc install-headers $(USERSPACE_DIRS:%=userspace-dir-install-%)
clean-userspace: $(USERSPACE_DIRS:%=userspace-dir-clean-%)
clean-sbin: userspace-dir-clean-sbin

userspace-dir-%:
	$(MAKE) -C $*

userspace-dir-install-%:
	$(MAKE) -C $* install
	@touch $(BUILD_DIR)/sysroot_sha1

userspace-dir-clean-%:
	$(MAKE) -C $* clean
	@touch $(BUILD_DIR)/sysroot_sha1

#
# sysroot
#

sysroot: $(SYS_ROOT)

install-etc: $(wildcard etc/*)
	mkdir -p $(SYS_ROOT)/etc
	cp -r etc/* $(SYS_ROOT)/etc/

install-headers: $(BUILD_DIR)/linux-headers
	mkdir -p $(SYS_ROOT)/usr/include
	rm -f $(SYS_ROOT)/usr/include/{linux,asm,asm-generic}

	$(MAKE) -C toolchain musl-headers DESTDIR=$(SYS_ROOT)/usr
	ln -sf $(BUILD_DIR)/linux-headers/linux $(SYS_ROOT)/usr/include/linux
	ln -sf $(BUILD_DIR)/linux-headers/asm-generic $(SYS_ROOT)/usr/include/asm-generic
	ln -sf $(BUILD_DIR)/linux-headers/asm $(SYS_ROOT)/usr/include/asm

.PHONY: $(SYS_ROOT)
$(SYS_ROOT): install-headers install-userspace
	mkdir -p $(SYS_ROOT)/lib
	mkdir -p $(SYS_ROOT)/usr/lib

	cp $(TOOL_ROOT)/usr/lib/libc.so $(SYS_ROOT)/usr/lib/libc.so
	$(STRIP) $(SYS_ROOT)/usr/lib/libc.so
	ln -sf /usr/lib/libc.so $(SYS_ROOT)/lib/ld-musl-$(ARCH).so.1 || true
	@touch $(BUILD_DIR)/sysroot_sha1

# sysroot sha1sum
$(BUILD_DIR)/sysroot_sha1:
	echo "$$(tar -cf - $(SYS_ROOT) | sha1sum | awk '{print $$1}')" > $@

#
# external dependencies
#

musl:
	$(MAKE) -C third-party/musl

clean-musl:
	$(MAKE) -C third-party/musl clean

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
# development tools
#

tools: qemu-profile-plugin clang-tidy-plugin

# clang tidy plugin
ifeq ($(CLANG_BUILD_PLUGIN),y)
clang-tidy-plugin:
	$(MAKE) -C tools/clang-tidy-plugin
else
clang-tidy-plugin:
endif

# qemu profiling plugin
ifeq ($(QEMU_BUILD_PLUGIN),y)
qemu-profile-plugin:
	$(MAKE) -C tools/qemu-profile-plugin
else
qemu-profile-plugin:
endif

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

print-make-var:
ifndef VAR
	$(error VAR is undefined)
endif
	@printf "$($(VAR))"

print-module-var:
ifndef VAR
	$(error VAR is undefined)
endif
ifndef MODULE
	$(error MODULE is undefined)
endif
	@echo "VAR: $(VAR)"
	@echo "MODULE: $(MODULE)"
	@echo "value: $(call module-var,$(VAR),$(MODULE))"

# ------------------- #
#  Compilation Rules  #
# ------------------- #

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/%.c
	@mkdir -p $(@D)
	$(call var,CC,$<) $(call var,INCLUDE,$<) $(call var,CFLAGS,$<) $(call var,DEFINES,$<) -o $@ -c $<

$(OBJ_DIR)/%.c.d: $(PROJECT_DIR)/%.c
	@mkdir -p $(@D)
	$(call var,CC,$<) $(call var,INCLUDE,$<) $(call var,CFLAGS,$<) $(call var,DEFINES,$<) \
		-MM -MT $(@:%.d=%.o) -MF $@ $<

$(OBJ_DIR)/%.cpp.o: $(PROJECT_DIR)/%.cpp
	@mkdir -p $(@D)
	$(call var,CXX,$<) $(call var,INCLUDE,$<) $(call var,CXXFLAGS,$<) $(call var,DEFINES,$<) -o $@ -c $<

$(OBJ_DIR)/%.s.o: $(PROJECT_DIR)/%.s
	@mkdir -p $(@D)
	$(call var,AS,$<) $(call var,INCLUDE,$<) $(call var,ASFLAGS,$<) -o $@ $<

$(OBJ_DIR)/%.asm.o: $(PROJECT_DIR)/%.asm
	@mkdir -p $(@D)
	$(call var,NASM,$<) $(call var,INCLUDE,$<) $(call var,NASMFLAGS,$<) -o $@ $<

-include $(KERNEL_OBJECTS:.o=.d)

endif
