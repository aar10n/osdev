//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <Common.h>
#include <Config.h>
#include <File.h>
#include <Memory.h>
#include <Loader.h>
#include <Video.h>

#define CHECK_ERROR(status) \
  if (EFI_ERROR(status)) { \
    PRINT_STATUS(Status); \
    return status; \
  }

#define BLUE_COLOR 0x000000FF
#define GREEN_COLOR 0x0000FF00
#define RED_COLOR 0x00FF0000

typedef __attribute__((sysv_abi)) void (*KERNEL_ENTRY)(UINT64 StackPtr, UINT64 BootInfo);

CONST CHAR16 *DefaultKernelPath = L"/EFI/BOOT/kernel.elf";
CONST UINT64 KernelPhysAddr = 0x100000;
// CONST UINT64 KernelVirtAddr = 0xFFFFFF8000000000;

BOOLEAN Debug;
BOOLEAN FastBoot;
CHAR16 *KernelImagePath;

// state of virtual memory when entering kernel
// --------------------------------------------
// 0x - @+4GiB                  Identity mapped
// 0xFFFFFF8000000000 - @+4MiB  Kernel


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

  // Set video mode if requested
  UINT32 XResolution, YResolution;
  Status = ConfigGetDimensions("video", &XResolution, &YResolution);
  if (!EFI_ERROR(Status)) {
    GRAPHICS_MODE_INFO *GraphicsMode;
    Status = SelectVideoMode(1280, 768, &GraphicsMode);
    CHECK_ERROR(Status);
  }

  // Load kernel
  UINT64 KernelEntry = 0;
  MEMORY_DESCRIPTOR *KernelPages = NULL;
  Status = LoadKernel(KernelImagePath, KernelPhysAddr, &KernelEntry, &KernelPages);
  CHECK_ERROR(Status);

  if (!FastBoot) {
    // slow boot
    // wait for user input before launching kernel
    PRINT_INFO("Press any key to continue...");
    Status = gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), NULL);
    CHECK_ERROR(Status);
  }

  UINT64 Address = (KernelEntry - 0x0000000000400000) + KernelPhysAddr;
  PRINT_INFO("Address: 0x%p", Address);

  PRINT_INFO("Done");

  EFI_MEMORY_MAP *MemoryMap = AllocateZeroPool(sizeof(EFI_MEMORY_MAP));
  Status = GetMemoryMap(MemoryMap);
  CHECK_ERROR(Status);

  Status = AddDescriptors(KernelPages, MemoryMap);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to exit boot services");
    return Status;
  }

  Status = ExitBootServices(MemoryMap);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to exit boot services");
    PRINT_STATUS(Status);
    return Status;
  }

  DrawSquare(0, 0, 100, 100, BLUE_COLOR);

  Status = SetVirtualAddressMap(MemoryMap);
  if (EFI_ERROR(Status)) {
    gRT->ResetSystem(EfiResetWarm, Status, 0, NULL);
    DrawSquare(0, 0, 100, 100, RED_COLOR);
    while (TRUE) CpuPause();
  } else {
    DrawSquare(0, 0, 100, 100, GREEN_COLOR);
  }

  // KERNEL_ENTRY KernelEntryFunc = (KERNEL_ENTRY)(KernelEntry - KernelVirtAddr);

  // KernelEntryFunc(0, 0);
  // gRT->ResetSystem(EfiResetWarm, EFI_ABORTED, 0, NULL);
  DrawSquare(100, 0, 100, 100, BLUE_COLOR);
  UINT64 X = *((UINT64 *) Address);
  DrawSquare(200, 0, 100, 100, X);
  // UINT64 X = *((UINT64 *)(UINTN) KernelEntryFunc);

  KERNEL_ENTRY KernelEntryFunc = (KERNEL_ENTRY) Address;
  KernelEntryFunc(0, 0);

  while (TRUE) CpuPause();
}

