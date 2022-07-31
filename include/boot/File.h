//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#ifndef BOOT_FILE_H
#define BOOT_FILE_H

#include <Common.h>

EFI_STATUS EFIAPI InitializeFileProtocols();
EFI_STATUS EFIAPI OpenFile(IN CONST CHAR16 *Path, OUT EFI_FILE **File);
EFI_STATUS EFIAPI ReadFile(IN EFI_FILE *File, OUT UINTN *BufferSize, OUT VOID **Buffer);

EFI_STATUS EFIAPI LocateFileByName(
  IN EFI_FILE *Parent,
  IN CONST CHAR16 *Name,
  IN BOOLEAN Recurse,
  OUT EFI_FILE **File
);

#endif
