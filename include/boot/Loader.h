//
// Created by Aaron Gill-Braun on 2022-05-30.
//

#ifndef BOOT_LOADER_H
#define BOOT_LOADER_H

#include <Common.h>
#include <Memory.h>

EFI_STATUS EFIAPI ReadElf(IN VOID *Buffer, OUT UINT64 *EntryPoint, OUT UINTN *MemSize);
EFI_STATUS EFIAPI LoadElf(IN VOID *Buffer, IN UINT64 PhysAddr, OUT MEMORY_DESCRIPTOR **Descriptors);
EFI_STATUS EFIAPI LoadKernel(
  IN CONST CHAR16 *Path,
  IN UINT64 PhysAddr,
  OUT UINT64 *Entry,
  OUT MEMORY_DESCRIPTOR **Descriptors
);

EFI_STATUS EFIAPI PrintElfHeader(IN VOID *Buffer);

#endif
