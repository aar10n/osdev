//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H

#include <stdint.h>
#include <boot.h>

typedef struct _EFI_MEMORY_MAP {
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MemoryMapSize;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;

EFI_STATUS EFIAPI GetMemoryMap(OUT EFI_MEMORY_MAP *Mmap);
EFI_STATUS EFIAPI CreateKernelMemoryMap(
  IN EFI_MEMORY_MAP *Mmap,
  OUT memory_map_t **KernelMapPtr,
  OUT UINTN *KernelMapSize,
  OUT UINTN *TotalMemory
);

void EFIAPI CreatePageTables(UINT64 RegionAddr, UINTN RegionSize, UINT64 Address);

void EFIAPI PrintMemoryMap(EFI_MEMORY_MAP *Mmap);
void EFIAPI PrintKernelMemoryMap(memory_map_t *MemoryMap, UINTN MemoryMapSize);

#endif
