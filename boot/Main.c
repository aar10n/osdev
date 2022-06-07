//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <Common.h>
#include <Config.h>
#include <File.h>
#include <Memory.h>
#include <Loader.h>
#include <System.h>
#include <Video.h>

#include <Guid/Acpi.h>
#include <Guid/SmBios.h>

#define CHECK_ERROR(status) \
  if (EFI_ERROR(status)) { \
    PRINT_STATUS(Status); \
    return status; \
  }

#define BLUE_COLOR 0x000000FF
#define GREEN_COLOR 0x0000FF00
#define RED_COLOR 0x00FF0000
#define QEMU_DEBUG_PORT 0x402


typedef void (*KERNEL_ENTRY)(boot_info_v2_t*) __attribute__((sysv_abi));

CHAR16 *DefaultKernelPath = L"/EFI/BOOT/kernel.elf";
UINT64 KernelPhysAddr = 0x100000;

EFI_GUID AcpiTableGuid = EFI_ACPI_20_TABLE_GUID;
EFI_GUID SmbiosTableGuid = SMBIOS_TABLE_GUID;

// config variables
BOOLEAN Debug;
BOOLEAN FastBoot;
CHAR16 *KernelImagePath;
//
UINT64 FramebufferBase;
UINT32 ScreenWidth;
UINT32 ScreenHeight;
CHAR8 DebugBuffer[1024];

BOOLEAN PostExitBootServices = FALSE;

CHAR8 EFIAPI *DebugUInt64ToDecStr(UINT64 Value) {
  CONST CHAR8 *Digits = "0123456789";

  CHAR8 *Buffer = DebugBuffer + 1023;
  *Buffer-- = '\0';
  if (Value == 0) {
    *Buffer-- = '0';
  } else {
    while (Value != 0) {
      *Buffer-- = Digits[Value % 10];
      Value /= 10;
    }
  }
  return Buffer + 1;
}

CHAR8 EFIAPI *DebugUInt64ToHexStr(UINT64 Value) {
  CONST CHAR8 *Digits = "0123456789ABCDEF";

  CHAR8 *Buffer = DebugBuffer + 1023;
  *Buffer-- = '\0';
  if (Value == 0) {
    *Buffer-- = '0';
  } else {
    while (Value != 0) {
      *Buffer-- = Digits[Value % 16];
      Value /= 16;
    }
  }
  return Buffer + 1;
}

VOID EFIAPI QemuDebugWriteStr(CONST CHAR8 *String) {
  while (*String) {
    IoWrite8(QEMU_DEBUG_PORT, *String);
    String++;
  }
}

VOID EFIAPI QemuDebugWriteHex(UINT64 Value) {
  QemuDebugWriteStr("0x");
  QemuDebugWriteStr(DebugUInt64ToHexStr(Value));
  QemuDebugWriteStr("\n");
}

VOID EFIAPI QemuDebugPrintMemoryMap(IN EFI_MEMORY_MAP *MemoryMap) {
  EFI_MEMORY_DESCRIPTOR *Desc = MemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, MemoryMap)) {
    QemuDebugWriteStr("Type: ");
    QemuDebugWriteStr(DebugUInt64ToDecStr(Desc->Type));
    QemuDebugWriteStr(" | VirtualStart: 0x");
    QemuDebugWriteStr(DebugUInt64ToHexStr(Desc->VirtualStart));
    QemuDebugWriteStr(" | PysicalStart: 0x");
    QemuDebugWriteStr(DebugUInt64ToHexStr(Desc->PhysicalStart));
    QemuDebugWriteStr(" | NumberOfPages: ");
    QemuDebugWriteStr(DebugUInt64ToDecStr(Desc->NumberOfPages));
    QemuDebugWriteStr("\n");

    Desc = MEMORY_MAP_NEXT(Desc, MemoryMap);
  }
}

//

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

//

EFI_STATUS EFIMAIN UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;
  gST->ConOut->ClearScreen(gST->ConOut);

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

  ScreenWidth = GraphicsMode->HorizontalResolution;
  ScreenHeight = GraphicsMode->VerticalResolution;
  Status = GetFramebufferInfo(&FramebufferBase, &FramebufferSize);
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

  // Setup page tables
  UINT64 PML4Address = 0;
  Status = SetupKernelPageTables(KernelPages, &PML4Address);
  CHECK_ERROR(Status);

  // --------------------------------------------------

  // Allocate memory for boot structure
  boot_info_v2_t *BootInfo = NULL;
  Status = AllocateBootInfoStruct(&BootInfo);
  CHECK_ERROR(Status);

  BootInfo->kernel_phys_addr = KernelPhysAddr;
  BootInfo->kernel_virt_addr = KernelPages->VirtAddr;
  BootInfo->kernel_size = (UINT32) KernelSize;
  BootInfo->pml4_addr = PML4Address;

  BootInfo->fb_addr = FramebufferBase;
  BootInfo->fb_size = FramebufferSize;
  BootInfo->fb_width = ScreenWidth;
  BootInfo->fb_height = ScreenHeight;
  BootInfo->fb_pixel_format = GetBootInfoPixelFormat(GraphicsMode->PixelFormat);

  // pre-allocate memory for the boot memory_map_t array
  VOID *BootMemoryMapBuffer = AllocateRuntimePages(MMAP_MAX_SIZE);
  if (BootMemoryMapBuffer == NULL) {
    PRINT_ERROR("Failed to allocate memory for boot memory map");
    return EFI_OUT_OF_RESOURCES;
  }

  BootInfo->mem_map.map = BootMemoryMapBuffer;
  BootInfo->mem_map.capacity = MMAP_MAX_SIZE;


  UINT64 AcpiPtr = 0;
  Status = EfiGetSystemConfigurationTable(&AcpiTableGuid, (VOID **) &AcpiPtr);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("No ACPI table found");
    return Status;
  }

  UINT64 SmbiosPtr = 0;
  Status = EfiGetSystemConfigurationTable(&SmbiosTableGuid, (VOID **) &SmbiosPtr);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("No SMBIOS table found");
    return Status;
  }

  if (!FastBoot) {
    // wait for user input before launching kernel
    PRINT_INFO(">> Press any key to continue <<");
    WaitForKeypress();
  }

  PRINT_INFO("Done");
  PRINT_INFO("Exiting UEFI...");

  // final efi memory map
  EFI_MEMORY_MAP *MemoryMap = AllocateZeroPool(sizeof(EFI_MEMORY_MAP));
  Status = GetMemoryMap(MemoryMap);
  CHECK_ERROR(Status);

  // NOTE: No use of any functions which might allocate/free memory
  //       after this point. This includes all console interfaces.

  BootInfo->mem_map.size = BootInfo->mem_map.capacity;
  Status = ConvertEfiMemoryMapToBootFormat(
    MemoryMap,
    KernelPages,
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
    DrawSquare(0, 0, 100, 100, RED_COLOR);
    // We cant print an error or return control to system firmware
    // here so we do a system reset instead.
    gRT->ResetSystem(EfiResetWarm, Status, 0, NULL);
    while (TRUE) CpuPause();
  } else {
    DrawSquare(0, 0, 100, 100, GREEN_COLOR);
  }

  // NOTE: Only get runtume services pointer after it has been relocated
  //       by the call to SetVirtualAddressMap.
  BootInfo->efi_runtime_services = (UINT64) gST->RuntimeServices;
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

