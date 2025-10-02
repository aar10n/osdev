//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <Common.h>
#include <Config.h>
#include <File.h>
#include <FwCfg.h>
#include <Memory.h>
#include <Loader.h>
#include <Video.h>

#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#include <Library/SerialPortLib.h>
#include <Library/PrintLib.h>

#define CHECK_ERROR(status) \
  if (EFI_ERROR(status)) { \
    PRINT_STATUS(Status);   \
    Print(L"Press any key to exit\n"); \
    WaitForKeypress(); \
    return status; \
  }

typedef void (*KERNEL_ENTRY)(boot_info_v2_t*) __attribute__((sysv_abi));

CHAR16 *DefaultKernelPath = L"/EFI/BOOT/kernel.elf";
UINT64 KernelPhysAddr = 0x0100000;
UINT64 InitrdPhysAddrBase = 0x1000000;

EFI_GUID Acpi10TableGuid = EFI_ACPI_TABLE_GUID;
EFI_GUID Acpi20TableGuid = EFI_ACPI_20_TABLE_GUID;
EFI_GUID SmbiosTableGuid = SMBIOS_TABLE_GUID;
EFI_GUID Smbios3TableGuid = SMBIOS3_TABLE_GUID;

// config variables
BOOLEAN Debug;
BOOLEAN FastBoot;
CHAR16 *KernelImagePath;
CHAR16 *InitrdImagePath;
//
UINT64 FramebufferBase;
UINT32 ScreenWidth;
UINT32 ScreenHeight;
CHAR8 DebugBuffer[1024];

BOOLEAN PostExitBootServices = FALSE;

EFI_STATUS EFIAPI AllocateBootInfoStruct(OUT boot_info_v2_t **BootInfo) {
  UINTN NumPages = EFI_SIZE_TO_PAGES(sizeof(boot_info_v2_t));
  *BootInfo = AllocateRuntimePages(NumPages);
  if (*BootInfo == NULL) {
    PRINT_ERROR("Failed to allocate memory for boot_info");
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem(*BootInfo, EFI_PAGES_TO_SIZE(NumPages));
  (*BootInfo)->magic[0] = 'B';
  (*BootInfo)->magic[1] = 'O';
  (*BootInfo)->magic[2] = 'O';
  (*BootInfo)->magic[3] = 'T';
  return EFI_SUCCESS;
}

CHAR8 *EFIAPI GetKernelCmdline(VOID) {
  CHAR8 *qemu_cmdline = FwCfgReadCmdline();
  CHAR8 *config_cmdline = ConfigGet("cmdline");
  CHAR8 *final_cmdline = NULL;

  if (qemu_cmdline != NULL && config_cmdline != NULL) {
    // Merge both command lines: <config> <qemu>
    UINTN qemu_len = AsciiStrLen(qemu_cmdline);
    UINTN config_len = AsciiStrLen(config_cmdline);
    UINTN total_len = config_len + 1 + qemu_len;

    final_cmdline = AllocateRuntimePool(total_len + 1);
    if (final_cmdline != NULL) {
      AsciiSPrint(final_cmdline, total_len + 1, "%a %a", config_cmdline, qemu_cmdline);
      PRINT_INFO("Merged command line: %a", final_cmdline);
    }
  } else if (qemu_cmdline != NULL) {
    PRINT_INFO("Using command line from QEMU: %a", qemu_cmdline);
    final_cmdline = qemu_cmdline;
  } else if (config_cmdline != NULL) {
    PRINT_INFO("Using command line from config: %a", config_cmdline);
    final_cmdline = config_cmdline;
  }

  return final_cmdline;
}

//

EFI_STATUS EFIMAIN UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;
  gST->ConOut->ClearScreen(gST->ConOut);
  SerialPortInitialize();

  // Initialize protocols
  Status = InitializeFileProtocols();
  CHECK_ERROR(Status);
  Status = InitializeVideoProtocols();
  CHECK_ERROR(Status);

  // Load config
  Status = InitializeConfig();
  if (EFI_ERROR(Status) && Status != EFI_NOT_FOUND) {
    return Status;
  }

  Debug = ConfigGetBooleanD("debug", FALSE);
  FastBoot = ConfigGetBooleanD("fastboot", TRUE);
  KernelImagePath = ConfigGetStringD("kernel", DefaultKernelPath);
  InitrdImagePath = ConfigGetStringD("initrd", NULL);

  // Set video mode if specified
  GRAPHICS_MODE_INFO *GraphicsMode = NULL;
  UINTN FramebufferSize = 0;
  UINT32 XResolution, YResolution;
  Status = ConfigGetDimensions("video", &XResolution, &YResolution);
  if (EFI_ERROR(Status)) {
    Status = SelectVideoMode(0, 0, &GraphicsMode);
    CHECK_ERROR(Status);
  } else {
    Status = SelectVideoMode(XResolution, YResolution, NULL);
    CHECK_ERROR(Status);
  }

  Status = GetFramebufferInfo(&FramebufferBase, &FramebufferSize, &ScreenWidth, &ScreenHeight);
  CHECK_ERROR(Status);

  // Get initial memory map
  EFI_MEMORY_MAP *MemoryMap = AllocateZeroPool(sizeof(EFI_MEMORY_MAP));
  Status = GetMemoryMap(MemoryMap);
  CHECK_ERROR(Status);

  // Load kernel
  UINT64 KernelEntry = 0;
  UINTN KernelSize = 0;
  UINT64 BootInfoSymAddress = 0;
  PAGE_DESCRIPTOR *KernelPages = NULL;
  Status = LoadKernel(
    KernelImagePath,
    KernelPhysAddr,
    &KernelEntry,
    &KernelSize,
    &BootInfoSymAddress,
    &KernelPages
  );
  CHECK_ERROR(Status);
  ASSERT(KernelSize <= KERNEL_MAX_SIZE);

  // Load initrd
  UINT64 InitrdPhysAddr = 0;
  UINTN InitrdSize = 0;
  if (InitrdImagePath != NULL) {
    PRINT_INFO("Loading initrd");
    Status = LoadRawFile(InitrdImagePath, MemoryMap, InitrdPhysAddrBase, &InitrdPhysAddr, &InitrdSize);
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("Failed to load initrd");
      CHECK_ERROR(Status);
      UNREACHABLE();
    }
    PRINT_INFO("Loaded initrd");
  }

  // Setup page tables
  UINT64 PML4Address = 0;
  Status = SetupKernelPageTables(KernelPages, &PML4Address);
  CHECK_ERROR(Status);

  // --------------------------------------------------

  CHAR8 *final_cmdline = GetKernelCmdline();

  void *cmd_line = NULL;
  UINT32 cmdline_len = 0;

  if (final_cmdline != NULL) {
    UINTN len = AsciiStrLen(final_cmdline);
    ASSERT(len <= UINT32_MAX);

    cmd_line = AllocateRuntimePool(len + 1);
    if (cmd_line == NULL) {
      PRINT_ERROR("Failed to allocate memory for command line parameters");
      return EFI_OUT_OF_RESOURCES;
    }

    cmdline_len = (UINT32)len;
    CopyMem(cmd_line, final_cmdline, len);
    ((CHAR8 *)cmd_line)[len] = '\0';
  }

  // Allocate memory for boot structure
  boot_info_v2_t *BootInfo = NULL;
  Status = AllocateBootInfoStruct(&BootInfo);
  CHECK_ERROR(Status);

  BootInfo->kernel_phys_addr = KernelPhysAddr;
  BootInfo->kernel_virt_addr = KernelPages->VirtAddr;
  BootInfo->kernel_size = (UINT32) KernelSize;
  BootInfo->pml4_addr = PML4Address;

  BootInfo->cmdline = cmd_line;
  BootInfo->cmdline_len = cmdline_len;

  BootInfo->fb_addr = FramebufferBase;
  BootInfo->fb_size = FramebufferSize;
  BootInfo->fb_width = ScreenWidth;
  BootInfo->fb_height = ScreenHeight;
  BootInfo->fb_pixel_format = GetBootInfoPixelFormat(GraphicsMode->PixelFormat);

  BootInfo->initrd_addr = InitrdPhysAddr;
  BootInfo->initrd_size = InitrdSize;

  // pre-allocate memory for the boot memory_map_t array
  VOID *BootMemoryMapBuffer = AllocateRuntimePages(MMAP_MAX_SIZE);
  if (BootMemoryMapBuffer == NULL) {
    PRINT_ERROR("Failed to allocate memory for boot memory map");
    return EFI_OUT_OF_RESOURCES;
  }

  BootInfo->mem_map.map = BootMemoryMapBuffer;
  BootInfo->mem_map.capacity = MMAP_MAX_SIZE;


  UINT64 AcpiPtr = 0;
  Status = EfiGetSystemConfigurationTable(&Acpi20TableGuid, (VOID **) &AcpiPtr);
  if (EFI_ERROR(Status)) {
    Status = EfiGetSystemConfigurationTable(&Acpi10TableGuid, (VOID **) &AcpiPtr);
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("No ACPI tables found");
      return Status;
    }
  }

  UINT64 SmbiosPtr = 0;
  Status = EfiGetSystemConfigurationTable(&Smbios3TableGuid, (VOID **) &SmbiosPtr);
  if (EFI_ERROR(Status)) {
    Status = EfiGetSystemConfigurationTable(&SmbiosTableGuid, (VOID **) &SmbiosPtr);
    if (EFI_ERROR(Status)) {
      PRINT_WARN("No SMBIOS tables found");
      SmbiosPtr = 0;
    }
  }

  if (!FastBoot) {
    // wait for user input before launching kernel
    PRINT_INFO(">> Press any key to continue <<");
    WaitForKeypress();
  }

  PRINT_INFO("Done");
  PRINT_INFO("Exiting UEFI...");

  // get final efi memory map
  Status = GetMemoryMap(MemoryMap);
  CHECK_ERROR(Status);

  // NOTE: No use of any uefi system functions which might allocate/free
  //       memory after this point. This includes all console interfaces.

  BootInfo->mem_map.size = BootInfo->mem_map.capacity;
  Status = ConvertEfiMemoryMapToBootFormat(
    MemoryMap,
    BootMemoryMapBuffer,
    &BootInfo->mem_map.size,
    &BootInfo->mem_total
  );
  CHECK_ERROR(Status);

  Status = ExitBootServices(MemoryMap);
  PostExitBootServices = TRUE;
  CHECK_ERROR(Status);

  // --------------------------------------------------
  // NOTE: No more boot services available after this point.

  // switch to new page tables
  AsmWriteCr3(PML4Address);

  // relocate efi services and prepare for virtual memory
  Status = SetVirtualAddressMap(MemoryMap);
  if (EFI_ERROR(Status)) {
    // We cant print an error or return control to system firmware
    // here so we do a system reset instead.
    gRT->ResetSystem(EfiResetWarm, Status, 0, NULL);
    while (TRUE) CpuPause();
  }

  // NOTE: Only get runtume services pointer after it has been relocated
  //       by the call to SetVirtualAddressMap.
  BootInfo->efi_runtime_services = (UINT32)((UINT64) gST->RuntimeServices);
  BootInfo->acpi_ptr = (UINT32) AcpiPtr;
  BootInfo->smbios_ptr = (UINT32) SmbiosPtr;

  if (BootInfoSymAddress != 0) {
    // assign boot info pointer directly to symbol
    *((boot_info_v2_t **) BootInfoSymAddress) = BootInfo;
  }

  // enter the kernel
  ((KERNEL_ENTRY)KernelEntry)(BootInfo);
  gRT->ResetSystem(EfiResetCold, EFI_ABORTED, 0, NULL);
  while (TRUE) CpuPause();
}

