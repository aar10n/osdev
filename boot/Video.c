//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Video.h>

#define CHECK_STATUS(Status) \
  if (EFI_ERROR(Status)){ \
    return Status; \
  } \
  NULL

EFI_GRAPHICS_OUTPUT_PROTOCOL *Graphics;

EFI_STATUS EFIAPI VideoSet(UINT32 X, UINT32 Y, EFI_HANDLE *VideoDevice) {
  EFI_STATUS Status;

  EFI_HANDLE_PROTOCOL HandleProtocol = gST->BootServices->HandleProtocol;

  UINTN NumHandles;
  EFI_HANDLE *GfxDevices;
  Status = gBS->LocateHandleBuffer(
      ByProtocol,
      &gEfiGraphicsOutputProtocolGuid,
      NULL,
      &NumHandles,
      &GfxDevices
  );
  CHECK_STATUS(Status);

  for (UINTN Index = 0; Index < NumHandles; Index ++) {
    EFI_HANDLE Handle = GfxDevices[Index];
    Status = HandleProtocol(Handle, &gEfiGraphicsOutputProtocolGuid, (void **) &Graphics);
    CHECK_STATUS(Status);

    // Print(L"\n--- Graphics Device %d ---\n", Index);
    // Print(L"Current mode: %d\n", Graphics->Mode->Mode);
    // Print(L"Framebuffer base: 0x%p\n", Graphics->Mode->FrameBufferBase);
    // Print(L"Framebuffer size: %u\n", Graphics->Mode->FrameBufferSize);
    // Print(L"Max mode: %d\n", Graphics->Mode->MaxMode);
    // Print(L"\n");

    for (UINT32 Mode = 0; Mode < Graphics->Mode->MaxMode; Mode++) {
      UINTN GfxInfoSize;
      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
      Status = Graphics->QueryMode(Graphics, Mode, &GfxInfoSize, &Info);
      CHECK_STATUS(Status);

      // Print(L"Mode %d\n", Mode);
      // Print(L"  Horizontal Resolution: %u\n", Info->HorizontalResolution);
      // Print(L"  Vertical Resolution: %u\n", Info->VerticalResolution);
      // Print(L"  Pixel Format: %u\n", Info->PixelFormat);
      // Print(L"  Pixel Bitmask: %u\n", Info->PixelInformation);
      // Print(L"  Pixels Per Scanline: %u\n", Info->PixelsPerScanLine);

      if (Info->HorizontalResolution == X && Info->VerticalResolution == Y) {
        Status = Graphics->SetMode(Graphics, Mode);
        if (EFI_ERROR(Status)) {
          FreePool(Info);
          FreePool(GfxDevices);
          return Status;
        }

        *VideoDevice = Handle;
        FreePool(Info);
        FreePool(GfxDevices);
        return EFI_SUCCESS;
      }

      FreePool(Info);
    }
  }

  FreePool(GfxDevices);
  return EFI_UNSUPPORTED;
}
