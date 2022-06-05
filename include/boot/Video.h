//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

#include <Common.h>

typedef EFI_GRAPHICS_OUTPUT_MODE_INFORMATION GRAPHICS_MODE_INFO;


EFI_STATUS EFIAPI InitializeVideoProtocols();
EFI_STATUS EFIAPI SelectVideoMode(UINT32 TargetX, UINT32 TargetY, OUT GRAPHICS_MODE_INFO **ModeInfo);
EFI_STATUS EFIAPI GetFramebufferInfo(OUT UINT64 *Base, OUT UINTN *Size);
UINT32 EFIAPI GetBootInfoPixelFormat(IN EFI_GRAPHICS_PIXEL_FORMAT Format);

VOID EFIAPI DrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
VOID EFIAPI DrawSquare(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);

EFI_STATUS EFIAPI PrintVideoModes();

#endif
