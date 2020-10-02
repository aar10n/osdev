#
# Boot Makefile
#

CC-boot = clang
LD-boot = lld-link

INCLUDE-boot = \
	-Iinclude/ \
	-Iinclude/boot \
	-Ithird-party/edk2/MdePkg/Include \
	-Ithird-party/edk2/MdePkg/Include/X64 \
	-Ithird-party/edk2/MdeModulePkg \
	-Ithird-party/edk2/MdeModulePkg/Include \
	-Ithird-party/edk2/UefiCpuPkg/Include

CFLAGS-boot = $(CFLAGS) \
	-gdwarf \
	-target x86_64-unknown-windows \
	-fshort-wchar -mno-red-zone

LDFLAGS-boot = $(LDFLAGS) -gdwarf

NASMFLAGS-boot = -g -fwin64

# boot
boot += Config.c Loader.c Memory.c Video.c

# boot-y
boot-y = $(call objects,boot,$(BUILD_DIR)/boot)
boot-y-debug = $(call debug-objects,boot,$(BUILD_DIR)/boot)
