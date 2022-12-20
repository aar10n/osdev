//
// Created by Aaron Gill-Braun on 2022-05-30.
//

#ifndef BOOT_LOADER_H
#define BOOT_LOADER_H

#include <Common.h>
#include <Memory.h>

EFI_STATUS EFIAPI ReadElf(IN VOID *Buffer, OUT UINT64 *EntryPoint, OUT UINTN *MemSize);
EFI_STATUS EFIAPI LoadElf(IN VOID *Buffer, IN UINT64 PhysAddr, OUT PAGE_DESCRIPTOR **Descriptors);
EFI_STATUS EFIAPI PrintElfInfo(IN VOID *Buffer);

/**
 * Loads the kernel ELF image at the given physical address.
 *
 * @param Path The path to the kernel ELF image.
 * @param PhysAddr The physical address to load the kernel at.
 * @param Entry [out] The entry point of the kernel.
 * @param KernelSize [out] The size of the kernel image.
 * @param BootInfoSymbol [out] The address of the boot info symbol.
 * @param Descriptors [out] The page descriptors for the kernel.
 * @return EFI_SUCCESS if the kernel was loaded successfully, otherwise an error code.
 */
EFI_STATUS EFIAPI LoadKernel(
  IN CONST CHAR16 *Path,
  IN UINT64 PhysAddr,
  OUT UINT64 *Entry,
  OUT UINTN *KernelSize,
  OUT UINT64 *BootInfoSymbol,
  OUT PAGE_DESCRIPTOR **Descriptors
);

/**
 * Loads a raw file into memory.
 *
 * @param Path The path to the file.
 * @param MemoryMap The current efi memory map.
 * @param LoadMinimumAddress The minimum address to load the file at.
 * @param FileAddr [out] The physcial address of the file in memory.
 * @param FileSize [out] The size of the file.
 * @return
 */
EFI_STATUS EFIAPI LoadRawFile(
  IN CONST CHAR16 *Path,
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINT64 LoadMinimumAddress,
  OUT UINT64 *FileAddr,
  OUT UINT64 *FileSize
);

#endif
