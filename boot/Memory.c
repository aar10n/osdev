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

#include <Memory.h>

#define MMAP_SIZE 1024

#define PT_OFFSET(a) (((a) >> 12) & 0x1FF)
#define PDT_OFFSET(a) (((a) >> 21) & 0x1FF)
#define PDPT_OFFSET(a) (((a) >> 30) & 0x1FF)
#define PML4_OFFSET(a) (((a) >> 39) & 0x1FF)

extern BOOLEAN PostExitBootServices;

// The share PML4 table. This is assigned
// when `CreatePageTables` is called.
static UINT64 *PML4;
static UINT64 NextTable;

// When populating the boot page tables, these
// arrays are used to associate a given index
// in a certain level with a page table.
static UINT64 *PageTables[TABLE_LENGTH * 3];

//

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
    case MEMORY_UNKNOWN:
      return L"Unknown";
    case MEMORY_RESERVED:
      return L"Reserved";
    case MEMORY_FREE:
      return L"Free";
    case MEMORY_ACPI:
      return L"ACPI";
    case MEMORY_MMIO:
      return L"MMIO";
    default:
      return L"Invalid Type";
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
    case EfiACPIMemoryNVS:
    case EfiACPIReclaimMemory:
      return MEMORY_ACPI;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
      return MEMORY_MMIO;
    default:
      return MEMORY_RESERVED;
  }
}

BOOLEAN EFIAPI IsContiguous(EFI_MEMORY_DESCRIPTOR *A, EFI_MEMORY_DESCRIPTOR *B) {
  UINT64 PhysEndA = A->PhysicalStart + EFI_PAGES_TO_SIZE(A->NumberOfPages);
  return PhysEndA == B->PhysicalStart;
}

BOOLEAN EFIAPI CanMergeRegion(EFI_MEMORY_DESCRIPTOR *A, EFI_MEMORY_DESCRIPTOR *B) {
  UINT32 MemTypeA = GetKernelMemoryType(A->Type);
  UINT32 MemTypeB = GetKernelMemoryType(B->Type);

  return MemTypeA == MemTypeB && IsContiguous(A, B);
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

//
// Kernel Memory Map
//

EFI_STATUS EFIAPI CreateKernelMemoryMap(IN EFI_MEMORY_MAP *Mmap, OUT memory_map_t **KernelMapPtr) {
  UINTN MmapNumEntries = Mmap->MemoryMapSize / Mmap->DescriptorSize;
  UINTN Size =  sizeof(memory_map_t) * MmapNumEntries;

  // Allocate for worst case scenario of a 1:1 number
  // of memory_map_t structs to EFI_MEMORY_DESCRIPTORs
  memory_map_t *MemoryMap = AllocateZeroPool(sizeof(memory_map_t) + Size);
  memory_region_t *KernelMap = (memory_region_t *) ((UINTN) MemoryMap + sizeof(memory_map_t));
  if (KernelMap == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  UINTN TotalMem = 0;
  EFI_MEMORY_DESCRIPTOR *Last = NULL;
  UINTN RealNumEntries = 0;
  memory_region_t *Ptr = KernelMap;
  EFI_MEMORY_DESCRIPTOR *Desc = Mmap->MemoryMap;
  while ((UINTN) Desc < (UINTN) Mmap->MemoryMap + Mmap->MemoryMapSize) {
    if (Desc->Type == EfiReservedMemoryType) {
      Desc = NEXT_MEMORY_DESCRIPTOR(Desc, Mmap->DescriptorSize);
      continue;
    }

    UINT32 MemoryType = GetKernelMemoryType(Desc->Type);
    if (Last && CanMergeRegion(Last, Desc)) {
      // combine contiguous regions of the same type
      Ptr->size += Desc->NumberOfPages * EFI_PAGE_SIZE;
    } else {
      if (Last && !IsContiguous(Last, Desc)) {
        // Fill in missing region with type UNKNOWN
        UINT64 LastEnd = Last->PhysicalStart + EFI_PAGES_TO_SIZE(Last->NumberOfPages);

        Ptr++;
        Ptr->type = MEMORY_UNKNOWN;
        Ptr->phys_addr = LastEnd;
        Ptr->size = Desc->PhysicalStart - LastEnd;

        TotalMem += Ptr->size;
      }

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
      TotalMem += Desc->NumberOfPages * EFI_PAGE_SIZE;
    }

    Last = Desc;
    Desc = NEXT_MEMORY_DESCRIPTOR(Desc, Mmap->DescriptorSize);
  }

  UINTN RealMapSize = RealNumEntries * sizeof(memory_map_t);
  MemoryMap->mem_total = TotalMem;
  MemoryMap->mmap_size = RealMapSize;
  MemoryMap->mmap_capacity = Size;
  MemoryMap->mmap = KernelMap;

  *KernelMapPtr = MemoryMap;
  return EFI_SUCCESS;
}

UINT64 EFIAPI SplitMemoryRegion(
  IN PLACEMENT_TYPE PlacementType,
  IN memory_map_t *MemoryMap,
  IN memory_region_t *Region,
  IN UINTN RegionSize,
  IN UINT64 Offset
) {
  ASSERT(MemoryMap->mmap_size < MemoryMap->mmap_capacity);

  memory_region_t *Mmap = MemoryMap->mmap;

  UINT64 Pointer = (UINT64) Region;
  UINT64 Address = Region->phys_addr + Offset;
  UINT64 MemoryMapStart = (UINT64) Mmap;
  UINT64 MemoryMapEnd = MemoryMapStart + MemoryMap->mmap_size;

  UINTN OldType = Region->type;
  UINTN OldSize = Region->size;
  UINTN OldStart = Region->phys_addr;
  UINTN OldEnd = Region->phys_addr + OldSize;

  if (PlacementType == HighestAddress) {
    //    v region
    // |ooxxx|  ->  |oo|xxx|
    //    ^ split
    CopyMem(Region + 1, Region, MemoryMapEnd - Pointer);
    // low block
    Region->size = OldSize - RegionSize;
    // region block
    (Region + 1)->type = MEMORY_RESERVED;
    (Region + 1)->phys_addr = OldEnd - RegionSize;
    (Region + 1)->size = RegionSize;

    MemoryMap->mmap_size += sizeof(memory_region_t);
    return (Region + 1)->phys_addr;
  } else if (PlacementType == LowestAddress) {
    // v region
    // |xxooo|  ->  |xx|ooo|
    //    ^ split
    CopyMem(Region + 1, Region, MemoryMapEnd - Pointer);
    // region block
    Region->type = MEMORY_RESERVED;
    Region->size = RegionSize;
    // high block
    (Region + 1)->type = OldType;
    (Region + 1)->phys_addr = OldStart + RegionSize;
    (Region + 1)->size = OldSize - RegionSize;

    MemoryMap->mmap_size += sizeof(memory_region_t);
    return Region->phys_addr;
  } else if (PlacementType == AtAddress) {
    //    v region start
    // |ooxxxoooo|  ->  |oo|xxx|oooo|
    //      ^ region end
    if (Address + RegionSize == MemoryMapEnd) {
      CopyMem(Region + 1, Region, MemoryMapEnd - Pointer);
      // low block
      Region->size = OldSize - RegionSize;
      // region block
      (Region + 1)->type = MEMORY_RESERVED;
      (Region + 1)->phys_addr = Address;
      (Region + 1)->size = RegionSize;

      MemoryMap->mmap_size += sizeof(memory_region_t);
    } else if (Address == Region->phys_addr) {
      CopyMem(Region + 1, Region, MemoryMapEnd - Pointer);
      // region block
      Region->type = MEMORY_RESERVED;
      Region->size = RegionSize;
      // high block
      (Region + 1)->type = OldType;
      (Region + 1)->phys_addr = OldStart + RegionSize;
      (Region + 1)->size = OldSize - RegionSize;

      MemoryMap->mmap_size += sizeof(memory_region_t);
    } else {
      // split both
      CopyMem(Region + 2, Region, MemoryMapEnd - Pointer);
      (Region + 1)->size = Region->size - RegionSize;
      Region->size = RegionSize;

      // low block
      Region->size = Address - OldStart;
      // region block
      (Region + 1)->type = MEMORY_RESERVED;
      (Region + 1)->phys_addr = Address;
      (Region + 1)->size = RegionSize;
      // high block
      (Region + 2)->type = OldType;
      (Region + 2)->phys_addr = (Address + RegionSize);
      (Region + 2)->size = OldEnd - (Address + RegionSize);

      MemoryMap->mmap_size += sizeof(memory_region_t) * 2;
    }

    return Address;
  }
  return 0;
}

EFI_STATUS EFIAPI LocateMemoryRegion(
  IN PLACEMENT_TYPE PlacementType,
  IN memory_map_t *MemoryMap,
  IN UINTN RegionSize,
  IN OUT UINT64 *Address
) {
  UINT64 Offset = 0;

  memory_region_t *Mmap = MemoryMap->mmap;
  memory_region_t *Region = NULL;
  memory_region_t *Ptr = MemoryMap->mmap;
  while ((UINTN) Ptr < (UINTN) Mmap + MemoryMap->mmap_size) {
    UINT64 PtrEnd = Ptr->phys_addr + Ptr->size;
    UINT64 RegionEnd = Ptr->phys_addr + RegionSize;

    if (Ptr->type == MEMORY_FREE && Ptr->size >= RegionSize) {
      if (PlacementType == HighestAddress) {
        if (!Region || Ptr->phys_addr > Region->phys_addr) {
          Region = Ptr;
          Offset = PtrEnd - (Ptr->phys_addr + RegionSize);
        }
      } else if (PlacementType == LowestAddress) {
        Region = Ptr;
        break;
      } else if (PlacementType == AboveAddress) {
        if (Ptr->phys_addr >= *Address && RegionEnd < PtrEnd) {
          PlacementType = AtAddress;
          Region = Ptr;
          Offset = 0;
          break;
        }
      } else if (PlacementType == AtAddress) {
        if (*Address >= Ptr->phys_addr && *Address < PtrEnd) {
          if (RegionEnd > PtrEnd) {
            ErrorPrint(L"[Loader] Unable to find region at 0x%p\n", *Address);
            return EFI_NOT_FOUND;
          }

          Region = Ptr;
          Offset = *Address - Ptr->phys_addr;
          break;
        }
      }
    }
    Ptr++;
  }

  if (Region == NULL) {
    ErrorPrint(L"[Loader] Failed to find region\n");
    return EFI_NOT_FOUND;
  }

  UINT64 Result = SplitMemoryRegion(
    PlacementType,
    MemoryMap,
    Region,
    RegionSize,
    Offset
  );

  *Address = Result;
  return EFI_SUCCESS;
}

//
// Paging
//

PAGE_DESCRIPTOR EFIAPI *MakePageDescriptor(
  UINT64 VirtAddr,
  UINT64 PhysAddr,
  UINTN NumPages,
  UINT16 Flags
) {
  PAGE_DESCRIPTOR *Desc = AllocateRuntimePool(sizeof(PAGE_DESCRIPTOR));
  if (Desc == NULL) {
    return NULL;
  }

  Desc->VirtAddr = VirtAddr;
  Desc->PhysAddr = PhysAddr;
  Desc->NumPages = NumPages;
  Desc->Flags = Flags;
  Desc->Next = NULL;
  return Desc;
}

void EFIAPI AddPageDescriptor(PAGE_DESCRIPTOR *List, PAGE_DESCRIPTOR *Descriptor) {
  PAGE_DESCRIPTOR *Desc = List;
  while (Desc->Next != NULL) {
    Desc = Desc->Next;
  }
  Desc->Next = Descriptor;
}

//

UINTN NumTablesUsed = 1;
void EFIAPI WalkPageTables(UINT64 VirtAddr, UINT64 PhysAddr, UINT16 Flags, UINT64 *Parent, UINT8 Level) {
  // Print(L"--- Walking Level %d ---\n", Level);
  ASSERT(Level <= 4 && Level < 0);

  UINTN Shift = 12 + ((Level - 1) * 9);
  UINTN Index = (VirtAddr >> Shift) & 0x1FF;

  if (Level == 1 || (Level == 2 && (Flags & (1 << 7)))) {
    Parent[Index] = PhysAddr | Flags;
    return;
  }

  UINTN Offset = (4 - Level) * 512;
  UINT64 *Table = PageTables[Offset + Index];
  if (Table == NULL) {
    // Print(L"Adding new table at index %d (level %d)\n", Index, Level);

    // Adding new table
    UINTN TableSize = (TABLE_LENGTH * sizeof(UINT64));
    UINTN NumTables = (NextTable - (UINT64) PML4) / TableSize;
    ASSERT(NumTables < (RESERVED_TABLES - 2));

    Table = (UINT64 *) NextTable;
    PageTables[Offset + Index] = Table;

    // Add to parent table
    Parent[Index] = NextTable | 0b11;
    NextTable = NEXT_TABLE(Table);
  }

  // Print(L"Parent: 0x%p\n", Parent);

  WalkPageTables(VirtAddr, PhysAddr, Flags, Table, Level - 1);
}

void EFIAPI CreatePageTables(UINT64 Address, PAGE_DESCRIPTOR *Descriptors) {
  ASSERT(PostExitBootServices);
  PML4 = (UINT64 *) Address;
  NextTable = NEXT_TABLE(PML4);

  // enable page size extensions
  AsmWriteCr4(AsmReadCr4() | (1 << 4));

  // Use the existing mappings for the first 1 GiB
  UINT64 *OldPML4 = (UINT64 *) AsmReadCr3();
  PML4[0] = OldPML4[0];

  PAGE_DESCRIPTOR *Desc = Descriptors;
  while (Desc) {
    // Print(L"----- Descriptor -----\n");
    // Print(L"  Number of pages: %d\n", Desc->NumPages);
    for (UINTN Page = 0; Page < Desc->NumPages; Page++) {
      UINT64 VirtAddr = Desc->VirtAddr + (EFI_PAGES_TO_SIZE(Page));
      UINT64 PhysAddr = Desc->PhysAddr + (EFI_PAGES_TO_SIZE(Page));

      // Print(L"PML4[%d][%d][%d][%d]\n",
      //       PML4_OFFSET(VirtAddr),
      //       PDPT_OFFSET(VirtAddr),
      //       PDT_OFFSET(VirtAddr),
      //       PT_OFFSET(VirtAddr));
      // Print(L"  VirtAddr: 0x%p\n", VirtAddr);
      // Print(L"  PhysAddr: 0x%p\n", PhysAddr);
      // Print(L"  Flags: %d\n", Desc->Flags);

      WalkPageTables(VirtAddr, PhysAddr, Desc->Flags, PML4, 4);
    }
    // Print(L"------------------------\n");

    Desc = Desc->Next;
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

void EFIAPI PrintKernelMemoryMap(memory_map_t *MemoryMap) {
  Print(L"[Loader] Memory map size: %u\n", MemoryMap->mmap_size);
  Print(L"[Loader] Memory map entries: %u\n", MemoryMap->mmap_size / sizeof(memory_region_t));

  Print(L"------ Kernel Memory Map ------\n");

  UINT64 FreeMemory = 0;
  UINT64 ReservedMemory = 0;

  memory_region_t *Ptr = MemoryMap->mmap;
  while ((UINTN) Ptr < (UINTN) MemoryMap->mmap + MemoryMap->mmap_size) {
    CHAR16 *MemoryType = KernelMemoryTypeToString(Ptr->type);

    Print(L"%s\n", MemoryType);
    Print(L"  Address: 0x%p\n", Ptr->phys_addr);
    Print(L"  Size: %u\n", Ptr->size);

    if (Ptr->type == MEMORY_FREE)
      FreeMemory += Ptr->size;
    else if (Ptr->type == MEMORY_RESERVED)
      ReservedMemory += Ptr->size;

    Ptr++;
  }
  Print(L"-------------------------------\n");
  Print(L"Free memory: %u\n", FreeMemory);
  Print(L"Reserved memory: %u\n", ReservedMemory);
  Print(L"Total memory: %u\n", MemoryMap->mem_total);
}

void EFIAPI PrintPageDescriptors(PAGE_DESCRIPTOR *Descriptors) {
  PAGE_DESCRIPTOR *Desc = Descriptors;
  while (Desc != NULL) {
    CHAR16 *Present = Desc->Flags & 1 ? L"P" : L" ";
    CHAR16 *Read = Desc->Flags & 2 ? L"RW" : L" ";
    CHAR16 *PageSize = Desc->Flags & (1 << 7) ? L"PS" : L" ";

    Print(L"Descriptor:\n");
    Print(L"  Physical address: 0x%p\n", Desc->PhysAddr);
    Print(L"  Virtual address: 0x%p\n", Desc->VirtAddr);
    Print(L"  Number of pages: %d\n", Desc->NumPages);
    Print(L"  Flags: %s %s %s (%d)\n", Present, Read, PageSize, Desc->Flags);

    Desc = Desc->Next;
  }
}
