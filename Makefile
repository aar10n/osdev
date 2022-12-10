NAME := osdev
PROJECT_DIR := $(shell pwd)
BUILD_DIR = $(PROJECT_DIR)/build
OBJ_DIR = $(BUILD_DIR)/$(NAME)
SYS_ROOT = $(BUILD_DIR)/sysroot

EDK_DIR = $(BUILD_DIR)/edk2
TOOLCHAIN_DIR = $(SYS_ROOT)

include scripts/utils.mk
include Makefile.local
include toolchain/toolchain.mk

CFLAGS += -std=gnu17 -Wall -MMD -ffreestanding -nostdlib
CXXFLAGS += -std=gnu++17 -Wall -MMD -ffreestanding -nostdlib -fno-rtti -fno-exceptions
LDFLAGS +=
ASFLAGS +=
NASMFLAGS +=

INCLUDE += -Iinclude/

ifeq ($(DEBUG),1)
CFLAGS += -gdwarf-5
CXXFLAGS += -gdwarf-5
LDFLAGS += -g
ASFLAGS += -gdwarf-5
NASMFLAGS += -g -F dwarf -O0
endif

# --------- #
#  Targets  #
# --------- #

modules := BOOT KERNEL
targets = apps boot drivers fs kernel lib
include $(foreach target,$(targets),$(target)/Makefile)
$(call init-modules,$(modules))


# build targets

all: $(BUILD_DIR)/osdev.img $(BUILD_DIR)/ext2.img

bootloader: $(BUILD_DIR)/boot$(WINARCH).efi
kernel: $(BUILD_DIR)/kernel.elf
ovmf: $(BUILD_DIR)/OVMF_$(WINARCH).fd
ext2_img: $(BUILD_DIR)/ext2.img

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

# misc

.PHONY: clean
clean:
	rm -f $(BUILD_DIR)/{*.efi,*.elf}
	rm -rf $(OBJ_DIR)
	mkdir $(OBJ_DIR)

clean-bootloader:
	rm -f $(BUILD_DIR)/boot$(WINARCH).efi
	rm -f $(BUILD_DIR)/loader{.dll,.lib}
	rm -rf $(OBJ_DIR)/boot

clean-bootloader-all: clean-bootloader
	rm -f $(BUILD_DIR)/static_library_files.lst
	rm -rf $(EDK_DIR)/Build/Loader

clean-kernel:
	rm -f $(BUILD_DIR)/osdev.img
	rm -f $(BUILD_DIR)/kernel.elf
	rm -rf $(OBJ_DIR)/{$(call join-comma,$(KERNEL_TARGETS))}


# ------------------- #
#     Bootloader      #
# ------------------- #

BOOT_CC = $(CLANG)

BOOT_CFLAGS = $(CFLAGS) \
	-target $(ARCH)-unknown-windows -fshort-wchar \
	-mno-red-zone -Wno-microsoft-static-assert

BOOT_LDFLAGS = \
	/NOLOGO /NODEFAULTLIB /IGNORE:4001 /IGNORE:4254 /OPT:REF /OPT:ICF=10 \
    /ALIGN:32 /FILEALIGN:32 /SECTION:.xdata,D /SECTION:.pdata,D /Machine:$(WINARCH) \
    /DLL /ENTRY:_ModuleEntryPoint /SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER /SAFESEH:NO \
    /BASE:0 /MERGE:.rdata=.data /MLLVM:-exception-model=wineh

BOOT_INCLUDE = $(INCLUDE) -Iinclude/boot \
	-I$(EDK_DIR)/MdePkg/Include -I$(EDK_DIR)/MdePkg/Include/$(WINARCH) \
	-I$(EDK_DIR)/MdeModulePkg -I$(EDK_DIR)/MdeModulePkg/Include \
	-I$(EDK_DIR)/UefiCpuPkg/Include


# edk2 library dependencies
$(BUILD_DIR)/static_library_files.lst: boot/LoaderPkg.dsc boot/Loader.inf
	@EDK2_DIR=$(EDK_DIR) EDK2_BUILD_TYPE=$(EDK2_BUILD) bash toolchain/edk2.sh build $(WINARCH) loader-lst

$(BUILD_DIR)/loader.dll: $(BUILD_DIR)/static_library_files.lst $(BOOT_OBJECTS)
	$(LLD_LINK) $(BOOT_LDFLAGS) /lldmap @$< /OUT:$@ $(BOOT_OBJECTS)

$(BUILD_DIR)/boot$(WINARCH).efi: $(BUILD_DIR)/loader.dll
	$(EDK_DIR)/BaseTools/BinWrappers/PosixLike/GenFw -e UEFI_APPLICATION -o $@ $^

# ------------------- #
#       Kernel        #
# ------------------- #

#KERNEL_CFLAGS = $(CFLAGS) -mcmodel=large -mno-red-zone -fno-stack-protector \
#				-fno-omit-frame-pointer -fstrict-volatile-bitfields -fno-builtin-memset \
#				$(KERNEL_DEFINES)

KERNEL_CC = $(CLANG)

KERNEL_CFLAGS = $(CFLAGS) -target $(ARCH) -mcmodel=large -mno-red-zone \
				-fno-stack-protector -fno-omit-frame-pointer -fno-builtin-memset \
				$(KERNEL_DEFINES)

KERNEL_LDFLAGS = $(LDFLAGS) -Tlinker.ld -nostdlib -z max-page-size=0x1000 -L$(SYS_ROOT)/usr/lib -L$(BUILD_DIR)

KERNEL_INCLUDE = $(INCLUDE) -Iinclude/kernel -Iinclude/fs -Ilib -I$(SYS_ROOT)/include

KERNEL_DEFINES = $(DEFINES) -D__KERNEL__


$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJECTS) $(BUILD_DIR)/libdwarf.a
	$(LD) $(KERNEL_LDFLAGS) $^ -l:libdwarf.a --no-relax -o $@

# libdwarf
$(BUILD_DIR)/libdwarf.a:
	bash toolchain/libdwarf.sh build $(WINARCH)

# bootable USB image
$(BUILD_DIR)/osdev.img: $(BUILD_DIR)/boot$(WINARCH).efi $(BUILD_DIR)/kernel.elf config.ini
	dd if=/dev/zero of=$@ bs=1k count=2880
	mformat -i $@ -f 2880 -v osdev ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $< ::/EFI/BOOT
	mcopy -i $@ config.ini ::/EFI/BOOT
	mcopy -i $@ $(BUILD_DIR)/kernel.elf ::/EFI/BOOT

# ------------------- #
#      External       #
# ------------------- #

$(BUILD_DIR)/OVMF_$(WINARCH).fd:
	@EDK2_DIR=$(EDK_DIR) EDK2_BUILD_TYPE=$(EDK2_BUILD) bash toolchain/edk2.sh build $(WINARCH) ovmf

$(BUILD_DIR)/ext2.img: $(SYS_ROOT) $(call pairs-src-paths, $(EXT2_DEPS))
	scripts/mkdisk.pl -o $@ -s 256M $(EXT2_DEPS)

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

-include $(call include-module-deps,$(modules))
