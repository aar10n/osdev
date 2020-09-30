//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeLib.h>

#include <Memory.h>

#define PT_OFFSET(a) (((a) >> 12) & 0x1FF)
#define PDT_OFFSET(a) (((a) >> 21) & 0x1FF)
#define PDPT_OFFSET(a) (((a) >> 30) & 0x1FF)
#define PML4_OFFSET(a) (((a) >> 39) & 0x1FF)

#define MMAP_SIZE 1024

extern BOOLEAN DidExitBootServices;

CHAR16 EFIAPI *MemoryTypeToString(EFI_MEMORY_TYPE MemoryType) {
  switch (MemoryType) {
    case EfiReservedMemoryType:
      return L"Reserved";
    case EfiLoaderCode:
      return L"Loader Code";
    case EfiLoaderData:
      return L"Loader Data";
    case EfiBootServicesCode:
      return L"Boot Services Code";
    case EfiBootServicesData:
      return L"Boot Services Data";
    case EfiRuntimeServicesCode:
      return L"Runtime Services Code";
    case EfiRuntimeServicesData:
      return L"Runtime Services Data";
    case EfiConventionalMemory:
      return L"Conventional Memory";
    case EfiUnusableMemory:
      return L"Unusable Memory";
    case EfiACPIReclaimMemory:
      return L"ACPI Reclaimable Memory";
    case EfiACPIMemoryNVS:
      return L"ACPI Memory NVS";
    case EfiMemoryMappedIO:
      return L"Memory Mapped I/O";
    case EfiMemoryMappedIOPortSpace:
      return L"Memory Mapped I/O Port Space";
    case EfiPalCode:
      return L"PAL Code";
    case EfiPersistentMemory:
      return L"Persistent Memory";
    default:
      return L"Unknown";
  }
}

CHAR16 EFIAPI *KernelMemoryTypeToString(UINT32 MemoryType) {
  switch (MemoryType) {
    case MEMORY_RESERVED:
      return L"Reserved";
    case MEMORY_FREE:
      return L"Free";
    case MEMORY_ACPI:
      return L"ACPI";
    case MEMORY_MMIO:
      return L"MMIO";
    default:
      return L"Unknown";
  }
}

UINT32 EFIAPI GetKernelMemoryType(EFI_MEMORY_TYPE MemoryType) {
  switch (MemoryType) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
      return MEMORY_FREE;
    case EfiACPIReclaimMemory:
      return MEMORY_ACPI;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
      return MEMORY_MMIO;
    default:
      return MEMORY_RESERVED;
  }
}

BOOLEAN EFIAPI CanMergeRegion(EFI_MEMORY_DESCRIPTOR *A, EFI_MEMORY_DESCRIPTOR *B) {
  UINT32 MemTypeA = GetKernelMemoryType(A->Type);
  UINT32 MemTypeB = GetKernelMemoryType(B->Type);

  UINT64 PhysEndA = A->PhysicalStart + (A->NumberOfPages * EFI_PAGE_SIZE);
  return MemTypeA == MemTypeB && PhysEndA == B->PhysicalStart;
}

//
// Memory Map
//

EFI_STATUS EFIAPI GetMemoryMap(OUT EFI_MEMORY_MAP *Mmap) {
  EFI_STATUS Status;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MemoryMapSize;
  UINTN MapKey;
  UINTN DescSize;
  UINT32 DescVersion;

  Print(L"[Loader] Getting memory map\n");

  MemoryMapSize = MMAP_SIZE;
  MemoryMap = AllocatePool(MemoryMapSize);
GetMmap:
  Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescSize, &DescVersion);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FreePool(MemoryMap);
    MemoryMap = AllocatePool(MemoryMapSize);
    if (MemoryMap == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    goto GetMmap;
  } else if (EFI_ERROR(Status)) {
    Print(L"[Loader] Failed to get memory map\n");
    FreePool(MemoryMap);
    return Status;
  }

  Mmap->MemoryMap = MemoryMap;
  Mmap->MemoryMapSize = MemoryMapSize;
  Mmap->MapKey = MapKey;
  Mmap->DescriptorSize = DescSize;
  Mmap->DescriptorVersion = DescVersion;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI CreateKernelMemoryMap(
  IN EFI_MEMORY_MAP *Mmap,
  OUT memory_map_t **KernelMapPtr,
  OUT UINTN *KernelMapSize,
  OUT UINTN *TotalMemory
) {
  UINTN MmapNumEntries = Mmap->MemoryMapSize / Mmap->DescriptorSize;
  UINTN Size =  sizeof(memory_map_t) * MmapNumEntries;

  // Allocate for worst case scenario of a 1:1 number
  // of memory_map_t structs to EFI_MEMORY_DESCRIPTORs
  memory_map_t *KernelMap = AllocatePool(Size);
  if (KernelMap == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  UINTN TotalMem = 0;
  EFI_MEMORY_DESCRIPTOR *Last = NULL;
  UINTN RealNumEntries = 0;
  memory_map_t *Ptr = KernelMap;
  EFI_MEMORY_DESCRIPTOR *Desc = Mmap->MemoryMap;
  while ((UINTN) Desc < (UINTN) Mmap->MemoryMap + Mmap->MemoryMapSize) {
    if (Desc->Type == EfiReservedMemoryType || Desc->Type == EfiMemoryMappedIO) {
      Desc = NEXT_MEMORY_DESCRIPTOR(Desc, Mmap->DescriptorSize);
      continue;
    }

    UINT32 MemoryType = GetKernelMemoryType(Desc->Type);
    if (Last && CanMergeRegion(Last, Desc)) {
      // combine contiguous regions of the same type
      Ptr->size += Desc->NumberOfPages * EFI_PAGE_SIZE;
    } else {
      // add new region
      if (RealNumEntries > 0) {
        Ptr++;
      }

      Ptr->type = MemoryType;
      Ptr->phys_addr = Desc->PhysicalStart;
      Ptr->size = Desc->NumberOfPages * EFI_PAGE_SIZE;

      RealNumEntries++;
    }

    if (Ptr->type != MEMORY_MMIO) {
      TotalMemory += Desc->NumberOfPages * EFI_PAGE_SIZE;
    }

    Last = Desc;
    Desc = NEXT_MEMORY_DESCRIPTOR(Desc, Mmap->DescriptorSize);
  }

  UINTN RealMapSize = RealNumEntries * sizeof(memory_map_t);

  *KernelMapPtr = KernelMap;
  *KernelMapSize = RealMapSize;
  *TotalMemory = TotalMem;

  return EFI_SUCCESS;
}

//
// Paging
//

void EFIAPI CreatePageTables(UINT64 RegionAddr, UINTN RegionSize, UINT64 Address) {
  ASSERT(DidExitBootServices);

  // enable page size extensions
  AsmWriteCr4(AsmReadCr4() | (1 << 4));

  // The first page table comes right after the kernel
  UINT64 *PML4 = (UINT64 *) Address;
  UINT64 *KernelPDPT = (UINT64 *) Address + EFI_PAGES_TO_SIZE(1);
  UINT64 *KernelPDT = (UINT64 *) Address + EFI_PAGES_TO_SIZE(2);
  UINT64 *KernelPT = (UINT64 *) Address + EFI_PAGES_TO_SIZE(3);

  UINTN ZeroPageIndex = PT_OFFSET(KERNEL_VA + (UINTN) PML4 + EFI_PAGES_TO_SIZE(4));
  UINTN RegionMaxIndex = PT_OFFSET(KERNEL_VA + RegionAddr + RegionSize);

  // Use the existing mappings for the first 1 GiB
  UINT64 *OldPML4 = (UINT64 *) AsmReadCr3();
  PML4[0] = OldPML4[0];

  // Add our new mappings to the top of virtual memory
  PML4[511] = (UINTN) KernelPDPT | 0b11;
  // Map the kernel region
  KernelPDPT[510] = (UINTN) KernelPDT | 0b11;
  KernelPDT[0] = (UINTN) KernelPT | 0b11;
  UINT64 PhysAddr = RegionAddr;
  for (UINTN Index = 0; Index < RegionMaxIndex; Index++) {
    if (Index != ZeroPageIndex) {
      KernelPT[Index] = PhysAddr | 0b11;
    }
    PhysAddr += 0x1000;
  }
}

//
// Debugging
//

void EFIAPI PrintMemoryMap(EFI_MEMORY_MAP *Mmap) {
  Print(L"[Loader] Memory map size: %u\n", Mmap->MemoryMapSize);
  Print(L"[Loader] Map key: %u\n", Mmap->MapKey);
  Print(L"[Loader] Descriptor size: %u\n", Mmap->DescriptorSize);
  Print(L"[Loader] Descriptor version: %u\n", Mmap->DescriptorVersion);

  Print(L"------ Memory Map ------\n");

  EFI_MEMORY_DESCRIPTOR *Desc = Mmap->MemoryMap;
  while ((UINTN) Desc < (UINTN) Mmap->MemoryMap + Mmap->MemoryMapSize) {
    CHAR16 *MemType = MemoryTypeToString(Desc->Type);

    Print(L"%s\n", MemType);
    Print(L"  Physical start: 0x%p\n", Desc->PhysicalStart);
    Print(L"  Virtual Start: 0x%p\n", Desc->VirtualStart);
    Print(L"  Number of pages: %u\n", Desc->NumberOfPages);
    Print(L"  Attribute: %u\n", Desc->Attribute);

    Desc = NEXT_MEMORY_DESCRIPTOR(Desc, Mmap->DescriptorSize);
  }
  Print(L"------------------------\n");
}

void EFIAPI PrintKernelMemoryMap(memory_map_t *MemoryMap, UINTN MemoryMapSize) {
  Print(L"[Loader] Memory map size: %u\n", MemoryMapSize);
  Print(L"[Loader] Memory map entries: %u\n", MemoryMapSize / sizeof(memory_map_t));

  Print(L"------ Kernel Memory Map ------\n");

  UINT64 FreeMemory = 0;
  UINT64 ReservedMemory = 0;
  UINT64 MaxAddress = 0;
  UINT64 MaxAddressSize = 0;

  memory_map_t *Ptr = MemoryMap;
  while ((UINTN) Ptr < (UINTN) MemoryMap + MemoryMapSize) {
    CHAR16 *MemoryType = KernelMemoryTypeToString(Ptr->type);

    Print(L"%s\n", MemoryType);
    Print(L"  Address: 0x%p\n", Ptr->phys_addr);
    Print(L"  Size: %u\n", Ptr->size);

    if (Ptr->type == MEMORY_FREE)
      FreeMemory += Ptr->size;
    else if (Ptr->type == MEMORY_RESERVED)
      ReservedMemory += Ptr->size;

    if (Ptr->phys_addr > MaxAddress && Ptr->type != MEMORY_MMIO) {
      MaxAddress = Ptr->phys_addr;
      MaxAddressSize = Ptr->size;
    }

    Ptr++;
  }
  Print(L"-------------------------------\n");
  Print(L"Free memory: %u\n", FreeMemory);
  Print(L"Reserved memory: %u\n", ReservedMemory);
  Print(L"Total memory: %u\n", MaxAddress + MaxAddressSize);
}
