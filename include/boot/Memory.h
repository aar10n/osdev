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
  UINTN AllocatedSize;
  UINTN Key;
  UINTN DescSize;
  UINT32 DescVersion;
} EFI_MEMORY_MAP;

typedef struct _MEMORY_DESCRIPTOR {
  UINT32 Type;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  EFI_VIRTUAL_ADDRESS VirtualStart;
  UINT64 NumberOfPages;
  UINT64 Attribute;
  struct _MEMORY_DESCRIPTOR *Next;
} MEMORY_DESCRIPTOR;

#define MEMORY_MAP_END(map) ((EFI_PHYSICAL_ADDRESS)(map)->Map + (map)->Size)
#define AT_MEMORY_MAP_END(desc, map) ((EFI_PHYSICAL_ADDRESS)(desc) >= MEMORY_MAP_END(map))
#define MEMORY_MAP_NEXT(desc, map) (NEXT_MEMORY_DESCRIPTOR(desc, (map)->DescSize))
#define MEMORY_MAP_ENTRY(map, index) ((EFI_MEMORY_DESCRIPTOR *)((UINTN)(map)->Map + ((index) * (map)->DescSize)))

#define PD_W 0x01 // Writeable
#define PD_X 0x02 // Executable
#define PD_P 0x04 // Present
#define PD_S 0x08 // Page size


MEMORY_DESCRIPTOR *NewDescriptor(
  IN MEMORY_DESCRIPTOR *Prev,
  IN EFI_MEMORY_TYPE Type,
  IN EFI_PHYSICAL_ADDRESS PhysicalStart,
  IN EFI_VIRTUAL_ADDRESS VirtualStart,
  IN UINT64 NumberOfPages,
  IN UINT64 Attribute
);

EFI_STATUS EFIAPI GetMemoryMap(IN OUT EFI_MEMORY_MAP *MemoryMap);
EFI_STATUS EFIAPI ExitBootServices(IN EFI_MEMORY_MAP *MemoryMap);
EFI_STATUS EFIAPI SetVirtualAddressMap(IN EFI_MEMORY_MAP *MemoryMap);
EFI_STATUS EFIAPI AddDescriptors(IN MEMORY_DESCRIPTOR *Descriptors, IN OUT EFI_MEMORY_MAP *MemoryMap);

EFI_STATUS EFIAPI LocateFreeMemoryRegion(
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINTN NumPages,
  IN UINT64 AboveAddr,
  OUT UINT64 *Addr
);

EFI_STATUS EFIAPI AddKernelReservedDescriptors(IN UINTN ReservedSize, IN OUT MEMORY_DESCRIPTOR *Descriptors);

EFI_STATUS EFIAPI MakeIdentityMapped(IN OUT EFI_MEMORY_MAP *MemoryMap);

void EFIAPI PrintEfiMemoryMap(IN EFI_MEMORY_MAP *MemoryMap);

#endif
