//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H

#include <Common.h>
#include <Library/BaseMemoryLib.h>

typedef struct _EFI_MEMORY_MAP {
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN Size;
  UINTN Key;
  UINTN DescSize;
  UINT32 DescVersion;
} EFI_MEMORY_MAP;

typedef struct _PAGE_DESCRIPTOR {
  EFI_PHYSICAL_ADDRESS PhysAddr;
  EFI_VIRTUAL_ADDRESS VirtAddr;
  UINT64 NumPages;
  UINT64 Flags;
  struct _PAGE_DESCRIPTOR *Next;
} PAGE_DESCRIPTOR;

// Page descriptor flags
#define PD_WRITE    0x01 // Read/Write
#define PD_EXECUTE  0x02 // Execute
#define PD_NOCACHE  0x04 // Cache disabled
#define PD_WRTHRU   0x08 // Write through
#define PD_SIZE_2MB 0x10 // Page size (2MiB)
#define PD_SIZE_1GB 0x20 // Page size (1GiB)

#define MEMORY_MAP_END(map) ((EFI_PHYSICAL_ADDRESS)(map)->Map + (map)->Size)
#define AT_MEMORY_MAP_END(desc, map) ((EFI_PHYSICAL_ADDRESS)(desc) >= MEMORY_MAP_END(map))
#define MEMORY_MAP_NEXT(desc, map) (NEXT_MEMORY_DESCRIPTOR(desc, (map)->DescSize))
#define MEMORY_MAP_ENTRY(map, index) ((EFI_MEMORY_DESCRIPTOR *)((UINTN)(map)->Map + ((index) * (map)->DescSize)))

PAGE_DESCRIPTOR EFIAPI *NewDescriptor(
  IN OUT PAGE_DESCRIPTOR *Prev,
  IN EFI_PHYSICAL_ADDRESS PhysAddr,
  IN EFI_VIRTUAL_ADDRESS VirtAddr,
  IN UINT64 NumPages,
  IN UINT64 Flags
);

PAGE_DESCRIPTOR EFIAPI *GetLastDescriptor(IN PAGE_DESCRIPTOR *List);
PAGE_DESCRIPTOR EFIAPI *AppendDescriptors(IN OUT PAGE_DESCRIPTOR *List, IN PAGE_DESCRIPTOR *Descriptors);
EFI_PHYSICAL_ADDRESS EFIAPI ConvertVirtToPhysFromDescriptors(IN PAGE_DESCRIPTOR *List, IN EFI_VIRTUAL_ADDRESS VirtAddr);

EFI_STATUS EFIAPI GetMemoryMap(IN OUT EFI_MEMORY_MAP *MemoryMap);
EFI_STATUS EFIAPI ExitBootServices(IN EFI_MEMORY_MAP *MemoryMap);
EFI_STATUS EFIAPI SetVirtualAddressMap(IN EFI_MEMORY_MAP *MemoryMap);

EFI_STATUS EFIAPI ConvertEfiMemoryMapToBootFormat(
  IN EFI_MEMORY_MAP *EfiMemoryMap,
  OUT VOID *MapBuffer,
  OUT UINT32 *MapSize,
  OUT UINT64 *TotalMem
);

EFI_STATUS EFIAPI LocateFreeMemoryRegion(
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINTN NumPages,
  IN UINT64 AboveAddr,
  OUT UINT64 *Addr
);

//

EFI_STATUS EFIAPI SetupKernelPageTables(IN PAGE_DESCRIPTOR *Descriptors, OUT UINT64 *PML4Address);

void EFIAPI PrintEfiMemoryMap(IN EFI_MEMORY_MAP *MemoryMap);

#endif
