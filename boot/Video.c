//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#include <Video.h>

#include <Library/DevicePathLib.h>

#define DELTA(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))

EFI_HANDLE *GraphicsDeviceHandle;
EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsDevice;


EFI_STATUS EFIAPI InitializeVideoProtocols() {
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN NumHandles;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiGraphicsOutputProtocolGuid,
    NULL,
    &NumHandles,
    &Handles
  );
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to locate graphics device handles");
    return Status;
  }

  for (UINTN Index = 0; Index < NumHandles; ++Index) {
    DevicePath = DevicePathFromHandle(Handles[Index]);
    if (DevicePath == NULL || DevicePath->Type == 0) {
      continue;
    }

    GraphicsDeviceHandle = Handles[Index];
    Status = gBS->HandleProtocol(
      GraphicsDeviceHandle,
      &gEfiGraphicsOutputProtocolGuid,
      (VOID **)&GraphicsDevice
    );
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("Failed to get EFI_GRAPHICS_OUTPUT_PROTOCOL for graphics device %d", Index);
      return Status;
    }

    break;
  }

  if (GraphicsDevice == NULL) {
    FreePool(Handles);
    PRINT_ERROR("Failed to find graphics device");
    return EFI_UNSUPPORTED;
  }

  CHAR16 *PathStr = ConvertDevicePathToText(DevicePath, TRUE, TRUE);
  PRINT_INFO("Selected graphics device: %s", PathStr);
  FreePool(PathStr);
  FreePool(Handles);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI SelectVideoMode(UINT32 TargetX, UINT32 TargetY, OUT GRAPHICS_MODE_INFO **ModeInfo) {
  if (GraphicsDevice == NULL) {
    return EFI_UNSUPPORTED;
  }

  EFI_STATUS Status;
  GRAPHICS_MODE_INFO *GraphicsInfo;
  UINTN GraphicsInfoSize;

  UINTN ClosestMode = 0;
  UINT32 ClosestX = 0;
  UINT32 ClosestY = 0;

  PRINT_INFO("Changing video mode");
  PRINT_INFO("Requested resolution: %dx%d", TargetX, TargetY);
  for (UINTN Mode = 0; Mode < GraphicsDevice->Mode->MaxMode; Mode++) {
    Status = GraphicsDevice->QueryMode(GraphicsDevice, Mode, &GraphicsInfoSize, &GraphicsInfo);
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("Failed to query graphics mode %d", Mode);
      continue;
    }

    UINT32 ModeX = GraphicsInfo->HorizontalResolution;
    UINT32 ModeY = GraphicsInfo->VerticalResolution;
    if (ModeX == TargetX && ModeY == TargetY) {
      Status = GraphicsDevice->SetMode(GraphicsDevice, Mode);
      if (EFI_ERROR(Status)) {
        PRINT_ERROR("Failed to set graphics mode");
        FreePool(GraphicsInfo);
        return Status;
      }

      gST->ConOut->ClearScreen(gST->ConOut);
      PRINT_INFO("Found graphics mode with matching resolution");
      PRINT_INFO("Using graphics mode: %dx%d", ModeX, ModeY);
      *ModeInfo = GraphicsInfo;
      return EFI_SUCCESS;
    }

    if (DELTA(ModeX, TargetX) < DELTA(ClosestX, TargetX) || DELTA(ModeY, TargetY) < DELTA(ClosestY, TargetY)) {
      ClosestMode = Mode;
      ClosestX = GraphicsInfo->HorizontalResolution;
      ClosestY = GraphicsInfo->VerticalResolution;
    }

    FreePool(GraphicsInfo);
  }

  // fallback to closest mode
  Status = GraphicsDevice->QueryMode(GraphicsDevice, ClosestMode, &GraphicsInfoSize, &GraphicsInfo);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to query graphics mode %d", ClosestMode);
    return Status;
  }

  Status = GraphicsDevice->SetMode(GraphicsDevice, ClosestMode);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to set graphics mode");
    FreePool(GraphicsInfo);
    return Status;
  }

  // print after mode change so logs dont get wiped
  gST->ConOut->ClearScreen(gST->ConOut);
  PRINT_WARN("No graphics mode with matching resolution found");
  PRINT_INFO("Falling back to graphics mode: %dx%d", ClosestX, ClosestY);
  *ModeInfo = GraphicsInfo;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI GetFramebufferInfo(OUT UINT64 *Base, OUT UINTN *Size) {
  if (GraphicsDevice == NULL) {
    return EFI_UNSUPPORTED;
  }

  *Base = (UINT64)GraphicsDevice->Mode->FrameBufferBase;
  *Size = GraphicsDevice->Mode->FrameBufferSize;
  return EFI_SUCCESS;
}

VOID EFIAPI DrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
  UINT32 *Framebuffer = (UINT32 *)GraphicsDevice->Mode->FrameBufferBase;
  UINTN FramebufferSize = GraphicsDevice->Mode->FrameBufferSize;
  UINTN Offset = (Y * GraphicsDevice->Mode->Info->HorizontalResolution) + X;
  if (Offset >= FramebufferSize) {
    return;
  }
  Framebuffer[Offset] = Color;
}

VOID EFIAPI DrawSquare(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
  for (UINT32 YOffset = 0; YOffset < Height; YOffset++) {
    for (UINT32 XOffset = 0; XOffset < Width; XOffset++) {
      DrawPixel(X + XOffset, Y + YOffset, Color);
    }
  }
}

//
// Debugging
//

CONST CHAR16 *PixelFormatToString(EFI_GRAPHICS_PIXEL_FORMAT Format) {
  switch (Format) {
    case PixelRedGreenBlueReserved8BitPerColor:
      return L"PixelRedGreenBlueReserved8BitPerColor";
    case PixelBlueGreenRedReserved8BitPerColor:
      return L"PixelBlueGreenRedReserved8BitPerColor";
    case PixelBitMask:
      return L"PixelBitMask";
    case PixelBltOnly:
      return L"PixelBltOnly";
    case PixelFormatMax:
      return L"PixelFormatMax";
    default:
      return L"Unknown";
  }
}

EFI_STATUS EFIAPI PrintVideoModes() {
  if (GraphicsDevice == NULL) {
    return EFI_UNSUPPORTED;
  }

  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *GraphicsInfo;
  UINTN GraphicsInfoSize;

  PRINT_INFO("Supported modes:");
  for (UINTN Mode = 0; Mode < GraphicsDevice->Mode->MaxMode; Mode++) {
    Status = GraphicsDevice->QueryMode(GraphicsDevice, Mode, &GraphicsInfoSize, &GraphicsInfo);
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("Failed to query graphics mode %d", Mode);
      continue;
    }

    CONST CHAR16 *PixelFormat = PixelFormatToString(GraphicsInfo->PixelFormat);
    PRINT_INFO("    %dx%d | %s", GraphicsInfo->HorizontalResolution, GraphicsInfo->VerticalResolution, PixelFormat);
    FreePool(GraphicsInfo);
  }

  return EFI_SUCCESS;
}
