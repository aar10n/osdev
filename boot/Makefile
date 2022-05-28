#
# Boot Makefile
#

CC-boot = $(CLANG)
LD-boot = $(LLD_LINK)

INCLUDE-boot = \
	-Iinclude/ \
	-Iinclude/boot \
	-I$(EDK_DIR)/MdePkg/Include \
	-I$(EDK_DIR)/MdePkg/Include/$(WINARCH) \
	-I$(EDK_DIR)/MdeModulePkg \
	-I$(EDK_DIR)/MdeModulePkg/Include \
	-I$(EDK_DIR)/UefiCpuPkg/Include

CFLAGS-boot = $(CFLAGS) \
	-target $(ARCH)-unknown-windows -fshort-wchar \
	-mno-red-zone -Wmicrosoft-static-assert

LDFLAGS-boot = \
	/NOLOGO /NODEFAULTLIB /IGNORE:4001 /IGNORE:4254 /OPT:REF /OPT:ICF=10 \
    /ALIGN:32 /FILEALIGN:32 /SECTION:.xdata,D /SECTION:.pdata,D /Machine:$(WINARCH) \
    /DLL /ENTRY:_ModuleEntryPoint /SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER /SAFESEH:NO \
    /BASE:0 /MERGE:.rdata=.data /MLLVM:-exception-model=wineh

NASMFLAGS-boot = -fwin64

ifeq ($(DEBUG),1)
CFLAGS-boot += -gdwarf
NASMFLAGS-boot += -g
endif


# boot
boot += Config.c Loader.c Memory.c Video.c

# boot-y
boot-y = $(call target-objects,boot)
