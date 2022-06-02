//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <Memory.h>

#include <Library/UefiBootServicesTableLib.h>


MEMORY_DESCRIPTOR *NewDescriptor(
  IN MEMORY_DESCRIPTOR *Prev,
  IN EFI_MEMORY_TYPE Type,
  IN EFI_PHYSICAL_ADDRESS PhysicalStart,
  IN EFI_VIRTUAL_ADDRESS VirtualStart,
  IN UINT64 NumberOfPages,
  IN UINT64 Attribute
) {
  MEMORY_DESCRIPTOR *Descriptor;
  Descriptor = AllocateReservedPool(sizeof(MEMORY_DESCRIPTOR));
  if (Descriptor == NULL) {
    PRINT_ERROR("Failed to allocate memory for memory descriptor");
    return NULL;
  }

  if (Prev != NULL) {
    Prev->Next = Descriptor;
  }

  Descriptor->Type = (UINT32) Type;
  Descriptor->PhysicalStart = PhysicalStart;
  Descriptor->VirtualStart = VirtualStart;
  Descriptor->NumberOfPages = NumberOfPages;
  Descriptor->Attribute = Attribute;
  Descriptor->Next = NULL;
  return Descriptor;
}


//

EFI_STATUS EFIAPI GetMemoryMap(OUT EFI_MEMORY_MAP *MemoryMap) {
  EFI_STATUS Status;
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  } else if (MemoryMap->Map != NULL) {
    FreePool(MemoryMap->Map);
  }

  MemoryMap->Map = NULL;
  MemoryMap->Size = 0;
  MemoryMap->AllocatedSize = 0;

RETRY:
  MemoryMap->Map = AllocateReservedPool(MemoryMap->Size);
  if (MemoryMap->Map == NULL) {
    PRINT_ERROR("Failed to allocate memory for memory map");
    return EFI_OUT_OF_RESOURCES;
  }

  MemoryMap->AllocatedSize = MemoryMap->Size;
  Status = gBS->GetMemoryMap(
    &MemoryMap->Size,
    MemoryMap->Map,
    &MemoryMap->Key,
    &MemoryMap->DescSize,
    &MemoryMap->DescVersion
  );
  if (EFI_ERROR(Status)) {
    if (Status == EFI_BUFFER_TOO_SMALL) {
      FreePool(MemoryMap->Map);
      MemoryMap->Size = MemoryMap->Size * 2;
      goto RETRY;
    }
    PRINT_ERROR("Failed to get memory map");
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI ExitBootServices(IN EFI_MEMORY_MAP *MemoryMap) {
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  return gBS->ExitBootServices(
    gImageHandle,
    MemoryMap->Key
  );
}

EFI_STATUS EFIAPI SetVirtualAddressMap(IN EFI_MEMORY_MAP *MemoryMap) {
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  return gRT->SetVirtualAddressMap(
    MemoryMap->Size,
    MemoryMap->DescSize,
    MemoryMap->DescVersion,
    MemoryMap->Map
  );
}

//

EFI_STATUS EFIAPI AddDescriptors(IN MEMORY_DESCRIPTOR *Descriptors, IN OUT EFI_MEMORY_MAP *MemoryMap) {
  if (Descriptors == NULL || MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  UINTN CurrentCount = MemoryMap->Size / MemoryMap->DescSize;
  UINTN MaxCount = MemoryMap->AllocatedSize / MemoryMap->DescSize;
  UINTN DescriptorCount = 0;
  MEMORY_DESCRIPTOR *Descriptor = Descriptors;
  while (Descriptor != NULL) {
    if (CurrentCount >= MaxCount) {
      return EFI_OUT_OF_RESOURCES;
    }

    EFI_MEMORY_DESCRIPTOR *Desc = MEMORY_MAP_ENTRY(MemoryMap, CurrentCount - 1);
    Desc->Type = Descriptor->Type;
    Desc->PhysicalStart = Descriptor->PhysicalStart;
    Desc->VirtualStart = Descriptor->VirtualStart;
    Desc->NumberOfPages = Descriptor->NumberOfPages;
    Desc->Attribute = Descriptor->Attribute;

    DescriptorCount++;
    Descriptor = Descriptor->Next;
  }

  MemoryMap->Size += DescriptorCount * MemoryMap->DescSize;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LocateFreeMemoryRegion(
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINTN NumPages,
  IN UINT64 AboveAddr,
  OUT UINT64 *Addr
) {
  if (ALIGN_VALUE(AboveAddr, EFI_PAGE_SIZE) != AboveAddr) {
    return EFI_INVALID_PARAMETER;
  }

  UINTN DesiredSize = EFI_PAGES_TO_SIZE(NumPages);
  EFI_MEMORY_DESCRIPTOR *Desc = MemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, MemoryMap)) {
    if (Desc->Type != EfiConventionalMemory) {
      goto NEXT;
    }

    UINT64 RangeStart = Desc->PhysicalStart;
    UINTN RangeSize = EFI_PAGES_TO_SIZE(Desc->NumberOfPages);
    if (RangeStart < AboveAddr) {
      if (RangeStart + RangeSize > (AboveAddr + DesiredSize)) {
        *Addr = AboveAddr;
        return EFI_SUCCESS;
      } else {
        goto NEXT;
      }
    }

    if (RangeSize >= DesiredSize) {
      *Addr = RangeStart;
      return EFI_SUCCESS;
    }

  NEXT:
    Desc = MEMORY_MAP_NEXT(Desc, MemoryMap);
  }

  return EFI_NOT_FOUND;
}

//

EFI_STATUS EFIAPI AddKernelReservedDescriptors(IN UINTN ReservedSize, IN OUT MEMORY_DESCRIPTOR *Descriptors) {
  ASSERT(Descriptors != NULL);

  MEMORY_DESCRIPTOR *Last = Descriptors;
  while (Last->Next != NULL) {
    Last = Last->Next;
  }

  UINT64 KernelEndPhys = Last->PhysicalStart + EFI_PAGES_TO_SIZE(Last->NumberOfPages);
  UINT64 KernelEndVirt = Last->VirtualStart + EFI_PAGES_TO_SIZE(Last->NumberOfPages);
  UINTN KernelOffset = Descriptors->PhysicalStart;
  UINTN KernelSize = KernelEndPhys - KernelOffset;

  UINTN RemainingSize = ReservedSize - (KernelSize + KernelOffset);
  Last = NewDescriptor(Last, EfiConventionalMemory, KernelEndVirt, KernelEndPhys, EFI_SIZE_TO_PAGES(RemainingSize), 0);
  if (Last == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

//
// Debugging
//

CONST CHAR16 *MemoryTypeToString(EFI_MEMORY_TYPE MemoryType) {
  switch (MemoryType) {
  case EfiReservedMemoryType:
    return L"EfiReserved";
  case EfiLoaderCode:
    return L"EfiLoaderCode";
  case EfiLoaderData:
    return L"EfiLoaderData";
  case EfiBootServicesCode:
    return L"EfiBootServicesCode";
  case EfiBootServicesData:
    return L"EfiBootServicesData";
  case EfiRuntimeServicesCode:
    return L"EfiRuntimeServicesCode";
  case EfiRuntimeServicesData:
    return L"EfiRuntimeServicesData";
  case EfiConventionalMemory:
    return L"EfiConventionalMemory";
  case EfiUnusableMemory:
    return L"EfiUnusableMemory";
  case EfiACPIReclaimMemory:
    return L"EfiACPIReclaimMemory";
  case EfiACPIMemoryNVS:
    return L"EfiACPIMemoryNVS";
  case EfiMemoryMappedIO:
    return L"EfiMemoryMappedIO";
  case EfiMemoryMappedIOPortSpace:
    return L"EfiMemoryMappedIOPortSpace";
  case EfiPalCode:
    return L"EfiPALCode";
  case EfiPersistentMemory:
    return L"EfiPersistentMemory";
  default:
    return L"Unknown";
  }
}

void EFIAPI PrintEfiMemoryMap(IN EFI_MEMORY_MAP *MemoryMap) {
  EFI_STATUS Status;
  PRINT_INFO("MemoryMapSize: %u", MemoryMap->Size);
  PRINT_INFO("MapKey: %u", MemoryMap->Key);
  PRINT_INFO("DescriptorSize: %u", MemoryMap->DescSize);
  PRINT_INFO("DescriptorVersion: %u", MemoryMap->DescVersion);
  PRINT_INFO("------ Memory Map ------");

  EFI_MEMORY_DESCRIPTOR *Desc = MemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, MemoryMap)) {
    UINT32 CursX = gST->ConOut->Mode->CursorColumn;
    UINT32 CursY = gST->ConOut->Mode->CursorRow;

    PRINT_INFO("%s", MemoryTypeToString(Desc->Type));
    PRINT_INFO("    Physical start: 0x%p", Desc->PhysicalStart);
    PRINT_INFO("    Virtual start: 0x%p", Desc->VirtualStart);
    PRINT_INFO("    Number of pages: %u", Desc->NumberOfPages);
    PRINT_INFO("    Attribute: %u", Desc->Attribute);

    PRINT_INFO("-- less --");
    Status = gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), NULL);
    if (EFI_ERROR(Status)) {
      PRINT_ERROR("Failed to wait for key");
      PRINT_STATUS(Status);
      return;
    }

    EFI_INPUT_KEY Key;
    gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);

    gST->ConOut->SetCursorPosition(gST->ConOut, CursX, CursY);
    Desc = MEMORY_MAP_NEXT(Desc, MemoryMap);
  }

  PRINT_INFO("------------------------");
}
