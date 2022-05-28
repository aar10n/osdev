NAME := osdev
PROJECT_DIR := $(shell pwd)
BUILD_DIR = $(PROJECT_DIR)/build
OBJ_DIR = $(BUILD_DIR)/$(NAME)
SYS_ROOT = $(BUILD_DIR)/sysroot

EDK_DIR = $(BUILD_DIR)/edk2
TOOLCHAIN_DIR = $(BUILD_DIR)/toolchain

include scripts/utils.mk

ifeq ($(call exists,Makefile.local),true)
include Makefile.local
endif

include scripts/toolchain.mk

# --------- #
#  Targets  #
# --------- #

targets = boot kernel fs drivers lib apps
include $(foreach target,$(targets),$(target)/Makefile)


info:
	@echo "ARCH: $(ARCH)"
	@echo "WINARCH: $(WINARCH)"
	@echo "TARGET: $(TARGET)"
	@echo "Toolchain:"
	@echo "  CC = $(call toolchain,$(FILE),CC)"
	@echo "Deps: $(EXT2_DEPS)"
	@echo "Deps: $(call pair-sources, $(EXT2_DEPS))"

# build targets

all: $(BUILD_DIR)/osdev.img

bootloader: $(BUILD_DIR)/boot$(WINARCH).efi

kernel: $(BUILD_DIR)/kernel.elf

ovmf: $(BUILD_DIR)/OVMF_$(WINARCH).fd

# run targets

run: $(BUILD_DIR)/osdev.img
	$(QEMU) $(QEMU_OPTIONS) -monitor stdio

debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) &
	$(GDB) -w \
		-ex "target remote localhost:1234" \
		-ex "add-symbol $(BUILD_DIR)/kernel.elf"

run-debug: $(BUILD_DIR)/osdev.img
	$(QEMU) -s -S $(QEMU_OPTIONS) -monitor telnet:127.0.0.1:55544,server,nowait &> $(BUILD_DIR)/output &

# misc

.PHONY: clean
clean:
	rm -f $(BUILD_DIR)/*.efi
	rm -f $(BUILD_DIR)/*.elf
	rm -rf $(OBJ_DIR)
	mkdir $(OBJ_DIR)

resolve:
	@echo "VAR=$(VAR)"
	@echo "TARGET=$(TARGET)"
	@echo "-> $(call resolve,$(VAR),$(TARGET))"

print-var:
	@echo "VAR=$(VAR)"
	@echo "-> $($(VAR))"

# ------------------- #
#     Bootloader      #
# ------------------- #

# edk2 library dependencies
$(BUILD_DIR)/static_library_files.lst: boot/LoaderPkg.dsc boot/Loader.inf
	@bash toolchain/edk2.sh build $(WINARCH) loader-lst

$(BUILD_DIR)/loader.dll: $(BUILD_DIR)/static_library_files.lst $(boot-y)
	$(call resolve,LD,boot) $(call resolve,LDFLAGS,boot) /lldmap @$< /OUT:$@

$(BUILD_DIR)/boot$(WINARCH).efi: $(BUILD_DIR)/loader.dll
	$(EDK_DIR)/BaseTools/BinWrappers/PosixLike/GenFw -e UEFI_APPLICATION -o $@ $^

# ------------------- #
#       Kernel        #
# ------------------- #

$(BUILD_DIR)/kernel.elf: $(kernel-y) $(fs-y) $(drivers-y) $(lib-y)
	$(call resolve,LD,kernel) $(call resolve,LDFLAGS,kernel) $^ -o $@

# bootable USB
$(BUILD_DIR)/osdev.img: $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf config.ini
	dd if=/dev/zero of=$@ bs=1k count=1440
	mformat -i $@ -f 1440 -v osdev ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $< ::/EFI/BOOT
	mcopy -i $@ config.ini ::/EFI/BOOT
	mcopy -i $@ $(BUILD_DIR)/kernel.elf ::/EFI

# ------------------- #
#      External       #
# ------------------- #

$(BUILD_DIR)/OVMF_$(WINARCH).fd:
	@EDK2_DIR=$(EDK_DIR) bash toolchain/edk2.sh build $(WINARCH) ovmf

$(BUILD_DIR)/ext2.img: $(SYS_ROOT) $(call pairs-src-paths, $(EXT2_DEPS))
	scripts/mkdisk.pl -o $@ -s 256M $(SYS_ROOT):/ $(EXT2_DEPS)

# ------------------- #
#  Compilation Rules  #
# ------------------- #

#.SECONDEXPANSION:
#$(OBJ_DIR)/%.elf: $$($$*-y)
#	$(call resolve,LD,$*) $(call resolve,LDFLAGS,$*) -r $^ -o $@

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/%.c
	@mkdir -p $(@D)
	$(call resolve,CC,$<) $(call resolve,INCLUDE,$<) $(call resolve,CFLAGS,$<) -o $@ -c $<

$(OBJ_DIR)/%.cpp.o: $(PROJECT_DIR)/%.cpp
	@mkdir -p $(@D)
	$(call resolve,CXX,$<) $(call resolve,INCLUDE,$<) $(call resolve,CXXFLAGS,$<) -o $@ -c $<

$(OBJ_DIR)/%.s.o: $(PROJECT_DIR)/%.s
	@mkdir -p $(@D)
	$(call resolve,AS,$<) $(call resolve,INCLUDE,$<) $(call resolve,ASFLAGS,$<) -o $@ $<

$(OBJ_DIR)/%.asm.o: $(PROJECT_DIR)/%.asm
	@mkdir -p $(@D)
	$(call resolve,NASM,$<) $(call resolve,INCLUDE,$<) $(call resolve,NASMFLAGS,$<) -o $@ $<
