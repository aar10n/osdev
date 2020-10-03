//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H

#include "boot.h"
#include <stdint.h>

#define KERNEL_PA 0x100000
#define KERNEL_OFFSET 0xFFFFFF8000000000
#define STACK_VA 0xFFFFFFA000000000
#define STACK_SIZE 0x4000 // 16 KiB
#define KERNEL_RESERVED 0x300000 // 3 MiB
// The number of pre-allocated tables for paging
#define RESERVED_TABLES 8
#define TABLE_LENGTH 512

#define NEXT_TABLE(Table) \
  ((UINT64)(Table) + (TABLE_LENGTH * sizeof(UINT64 *)))


typedef struct _EFI_MEMORY_MAP {
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MemoryMapSize;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;

typedef struct _PAGE_DESCRIPTOR {
  UINT64 VirtAddr;
  UINT64 PhysAddr;
  UINTN NumPages;
  UINT16 Flags;
  BOOLEAN Size2MiB;
  struct _PAGE_DESCRIPTOR *Next;
} PAGE_DESCRIPTOR;

typedef enum {
  HighestAddress, // Use the highest available region
  LowestAddress,  // Use the lowest available region
  AboveAddress,   // Use the next available region after address
  AtAddress,      // Try to use the given region address
} PLACEMENT_TYPE;

EFI_STATUS EFIAPI GetMemoryMap(OUT EFI_MEMORY_MAP *Mmap);
EFI_STATUS EFIAPI CreateKernelMemoryMap(IN EFI_MEMORY_MAP *Mmap, OUT memory_map_t **KernelMapPtr);
EFI_STATUS EFIAPI LocateMemoryRegion(
  IN PLACEMENT_TYPE PlacementType,
  IN memory_map_t *MemoryMap,
  IN UINTN RegionSize,
  IN OUT UINT64 *Address
);

PAGE_DESCRIPTOR EFIAPI *MakePageDescriptor(
  UINT64 VirtAddr,
  UINT64 PhysAddr,
  UINTN NumPages,
  UINT16 Flags
);
void EFIAPI AddPageDescriptor(PAGE_DESCRIPTOR *List, PAGE_DESCRIPTOR *Descriptor);
void EFIAPI CreatePageTables(UINT64 Address, PAGE_DESCRIPTOR *Descriptors);

void EFIAPI PrintMemoryMap(EFI_MEMORY_MAP *Mmap);
void EFIAPI PrintKernelMemoryMap(memory_map_t *MemoryMap);
void EFIAPI PrintPageDescriptors(PAGE_DESCRIPTOR *Descriptors);

#endif
