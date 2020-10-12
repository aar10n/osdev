//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

typedef EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GRAPHICS_MODE;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *Graphics;


EFI_STATUS EFIAPI VideoSet(UINT32 X, UINT32 Y, OUT EFI_HANDLE *VideoDevice);

#endif