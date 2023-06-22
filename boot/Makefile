# Boot sources
$(call register,boot,BOOT)

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


# boot/
boot += Main.c Config.c File.c Loader.c Memory.c Video.c
