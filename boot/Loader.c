//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <IndustryStandard/SmBios.h>
#include <Library/MpInitLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/Smbios.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <Guid/SmBios.h>

#include <Config.h>
#include <Memory.h>
#include <Video.h>

#include <Elf64.h>
#include <ElfCommon.h>

#define MAX_INFO_SIZE 1024
#define EFI_MAIN __attribute((used)) EFIAPI

#define CHECK_STATUS(Status) \
  if (EFI_ERROR(Status)){ \
    ErrorPrint(L"[Loader] Error code %d\n", Status); \
    return Status; \
  } \
  NULL

#define CHECK_NULL(Ptr) \
  if (Ptr == NULL) {    \
    return EFI_OUT_OF_RESOURCES; \
  }                     \
  NULL

#define label(L) L:

typedef __attribute__((sysv_abi)) void (*KERNEL_ENTRY)(UINT64 StackPtr, UINT64 BootInfo);

BOOLEAN PostExitBootServices = FALSE;

EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
EFI_MP_SERVICES_PROTOCOL *MpServices;
EFI_SMBIOS_PROTOCOL *Smbios;
EFI_DEVICE_PATH *RootPath;
EFI_FILE *Root;

CHAR16 *DefaultKernelFile = L"/EFI/kernel.elf";

// Buffers
UINT8 *ConfigBuffer = NULL;
UINT8 *KernelBuffer = NULL;

//

UINTN EFIAPI GetNextPathPart(CHAR16 *Path, CHAR16 **Part) {
  // Skip leading /'s
  while (*Path == L'/') {
    Path++;
  }

  CHAR16 *Start = Path;
  UINTN PartLen = 0;
  while (*Path && *Path != '/') {
    Path++;
    PartLen++;
  }

  *Part = Start;
  return PartLen;
}

//
// File Loading
//

EFI_STATUS EFIAPI LocateFile(EFI_FILE_HANDLE Parent, CHAR16 *Path, EFI_FILE_HANDLE *File) {
  EFI_STATUS Status;
  EFI_FILE_INFO *FileInfo;
  UINTN FileInfoSize;
  CHAR16 *FileName;

  CHAR16 *PathStart = Path;
  CHAR16 *PathEnd = Path + StrLen(Path);
  CHAR16 *PathPart = NULL;
  UINTN PartLen = GetNextPathPart(Path, &PathPart);

  FileInfo = AllocatePool(MAX_INFO_SIZE);
  CHECK_NULL(FileInfo);

  EFI_FILE_HANDLE Dir = Parent;
  Dir->SetPosition(Dir, 0);
  while (TRUE) {
    if (PartLen == 0) {
      return EFI_INVALID_PARAMETER;
    }

    FileInfoSize = MAX_INFO_SIZE;
    Status = Dir->Read(Dir, &FileInfoSize, (void *) FileInfo);
    if (FileInfoSize == 0) {
      ErrorPrint(L"[Loader] File not found: %s\n", PathStart);

      // File not found
      FreePool(FileInfo);
      return RETURN_NOT_FOUND;
    } else if (EFI_ERROR(Status)) {
      FreePool(FileInfo);
      return Status;
    }

    FileName = FileInfo->FileName;
    if (StrnCmp(FileName, PathPart, PartLen) != 0) {
      // not a match so skip
      continue;
    }

    PathPart += PartLen;
    if (PathPart == PathEnd) {
      // If there are no more parts in the path it means we
      // have found the file. Open the file and return.
      DEBUG((EFI_D_INFO, "[Loader] File found: %s\n", PathStart));

      EFI_FILE_HANDLE Result;
      Status = Dir->Open(Dir, &Result, FileName, EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        FreePool(FileInfo);
        return Status;
      }

      *File = Result;

      FreePool(FileInfo);
      return EFI_SUCCESS;
    }

    // If there is still more to the path we expect the current
    // path part to point to a directory.
    if (FileInfo->Attribute & EFI_FILE_DIRECTORY) {
      // Open the directory
      EFI_FILE_HANDLE NewDir;
      Status = Dir->Open(Dir, &NewDir, FileName, EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        FreePool(FileInfo);
        return Status;
      }

      NewDir->SetPosition(NewDir, 0);

      // Continue to look for the file in the new directory
      CHAR16 *NewPart;
      PartLen = GetNextPathPart(PathPart, &NewPart);
      PathPart = NewPart;

      Dir = NewDir;
      continue;
    } else {
      // Uh oh. This was supposed to be a directory.
      ErrorPrint(L"[Loader] Invalid file path: %s\n", PathStart);

      FreePool(FileInfo);
      return EFI_NOT_FOUND;
    }
  }
}

EFI_STATUS EFIAPI LoadFile(
  IN EFI_FILE_HANDLE File,
  OUT UINTN *BufferSize,
  OUT UINT8 **Buffer
) {
  EFI_STATUS Status;
  EFI_FILE_INFO *FileInfo;
  UINTN FileSize;
  UINT8 *FileData;

  FileInfo = AllocatePool(MAX_INFO_SIZE);
  CHECK_NULL(FileInfo);

  EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;

  UINTN FileInfoSize = MAX_INFO_SIZE;
  Status = File->GetInfo(File, &FileInfoGuid, &FileInfoSize, FileInfo);
  if (EFI_ERROR(Status)) {
    FreePool(FileInfo);
    return Status;
  }

  FileSize = FileInfo->FileSize;

  FileData = AllocateRuntimeZeroPool(FileSize);
  if (FileData == NULL) {
    FreePool(FileInfo);
    return EFI_OUT_OF_RESOURCES;
  }

  // Ensure we're reading from the start of the file
  Status = File->SetPosition(File, 0);
  if (EFI_ERROR(Status)) {
    FreePool(FileInfo);
    FreePool(FileData);
    return Status;
  }

  Status = File->Read(File, &FileSize, FileData);
  if (EFI_ERROR(Status) || FileSize != FileInfo->FileSize) {
    FreePool(FileInfo);
    FreePool(FileData);
    return Status;
  }

  *BufferSize = FileSize;
  *Buffer = FileData;

  FreePool(FileInfo);
  return EFI_SUCCESS;
}

//
// ELF Functions
//

EFI_STATUS EFIAPI ReadElf(
  IN void *ElfImage,
  OUT UINTN *Size,
  OUT UINT64 *Entry,
  OUT UINT64 *PhysAddr,
  OUT UINT64 *VirtAddr,
  OUT PAGE_DESCRIPTOR **PageLayout
) {
  Elf64_Ehdr *ElfHdr;
  UINTN Index;
  UINT8 IdentMagic[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};

  ElfHdr = (Elf64_Ehdr *) ElfImage;
  for (Index = 0; Index < 4; Index++) {
    if (ElfHdr->e_ident[Index] != IdentMagic[Index]) {
      ErrorPrint(L"[Loader] Invalid ELF header\n");
      return EFI_INVALID_PARAMETER;
    }
  }

  // Print(L"ELF Header:\n");
  // Print(L"  Magic: 0x7F 0x45 0x4C 0x46\n");
  // Print(L"  Class: ELF64 (%d)\n", ElfHdr->e_ident[EI_CLASS]);
  // Print(L"  Data: %d\n", ElfHdr->e_ident[EI_DATA]);
  // Print(L"  Version: %d\n", ElfHdr->e_ident[EI_VERSION]);
  // Print(L"  OS/ABI: %d\n", ElfHdr->e_ident[EI_OSABI]);
  // Print(L"  ABI Version: %d\n", ElfHdr->e_ident[EI_ABIVERSION]);
  // Print(L"  Type: %d\n", ElfHdr->e_type);
  // Print(L"  Machine: %d\n", ElfHdr->e_machine);
  // Print(L"  Entry point address: 0x%p\n", ElfHdr->e_entry);
  // Print(L"  Start of program headers: %u\n", ElfHdr->e_phoff);
  // Print(L"  Start of section headers: %u\n", ElfHdr->e_shoff);
  // Print(L"  Flags: 0x%x\n", ElfHdr->e_flags);
  // Print(L"  Size of this header: %u\n", ElfHdr->e_ehsize);
  // Print(L"  Size of program headers: %u\n", ElfHdr->e_phentsize);
  // Print(L"  Number of program headers: %d\n", ElfHdr->e_phnum);
  // Print(L"  Size of section headers: %u\n", ElfHdr->e_shentsize);
  // Print(L"  Number of section headers: %d\n", ElfHdr->e_shnum);
  // Print(L"  Section header string table index: %u\n", ElfHdr->e_shstrndx);

  BOOLEAN FoundFirst = FALSE;

  UINTN LoadedSize = 0;
  PAGE_DESCRIPTOR *First = NULL;
  PAGE_DESCRIPTOR *Last = NULL;
  Elf64_Phdr *ProgramHdr = ElfImage + ElfHdr->e_phoff;
  for (Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (ProgramHdr->p_type == PT_LOAD) {
      if (!FoundFirst) {
        *PhysAddr = ProgramHdr->p_paddr;
        *VirtAddr = ProgramHdr->p_vaddr;
        FoundFirst = TRUE;
      }

      // Print(L"--> Allocating PAGE_DESCRIPTOR\n");
      PAGE_DESCRIPTOR *Segment = AllocateRuntimePool(sizeof(PAGE_DESCRIPTOR));
      CHECK_NULL(Segment);

      // Print(L"Size: %u (%d pages)\n", ProgramHdr->p_memsz, EFI_SIZE_TO_PAGES(ProgramHdr->p_memsz));

      Segment->VirtAddr = ProgramHdr->p_vaddr;
      Segment->PhysAddr = (ProgramHdr->p_vaddr - KERNEL_OFFSET);
      Segment->NumPages = EFI_SIZE_TO_PAGES(ProgramHdr->p_memsz);
      Segment->Flags = (ProgramHdr->p_flags & PF_W) ? 0b11 : 0b1; // Read/Write
      Segment->Next = NULL;

      if (Last) {
        Last->Next = Segment;
      } else {
        First = Segment;
      }
      Last = Segment;

      LoadedSize += ALIGN_VALUE(ProgramHdr->p_memsz, EFI_PAGE_SIZE);
      ProgramHdr = (Elf64_Phdr *) ((UINTN) ProgramHdr + ElfHdr->e_phentsize);
    }
  }

  *Entry = ElfHdr->e_entry;
  *Size = LoadedSize;
  *PageLayout = First;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LoadElf(void *ElfImage, UINT64 Address) {
  ASSERT(PostExitBootServices);

  Elf64_Ehdr *ElfHdr;
  UINTN Index;
  UINT8 IdentMagic[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};

  ElfHdr = (Elf64_Ehdr *) ElfImage;
  for (Index = 0; Index < 4; Index++) {
    if (ElfHdr->e_ident[Index] != IdentMagic[Index]) {
      ErrorPrint(L"[Loader] Invalid ELF header\n");
      return EFI_INVALID_PARAMETER;
    }
  }

  Elf64_Phdr *ProgramHdr = ElfImage + ElfHdr->e_phoff;
  for (Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (ProgramHdr->p_type == PT_LOAD) {
      void *FileSegment;
      void *MemSegment;
      void *Zeros;
      UINTN ZerosCount;

      FileSegment = (void *) ((UINTN) ElfImage + ProgramHdr->p_offset);
      MemSegment = (void *) (Address + (ProgramHdr->p_vaddr - (KERNEL_OFFSET + KERNEL_PA)));
      CopyMem(MemSegment, FileSegment, ProgramHdr->p_filesz);

      // Zero uninitialized data sections
      Zeros = (UINT8 *) MemSegment + ProgramHdr->p_filesz;
      ZerosCount = ProgramHdr->p_memsz - ProgramHdr->p_filesz;
      if (ZerosCount > 0) {
        SetMem(Zeros, ZerosCount, 0);
      }

      ProgramHdr = (Elf64_Phdr *) ((UINTN) ProgramHdr + ElfHdr->e_phentsize);
    }
  }

  return EFI_SUCCESS;
}

//
// System Information
//

// Determines the number of physical cores and threads in the system
EFI_STATUS EFIAPI SmbiosGetProcessorCount(UINTN *CoreCount, UINTN *ThreadCount) {
  EFI_STATUS Status;
  EFI_SMBIOS_HANDLE Handle = 0xFFFE;
  SMBIOS_TYPE Type = EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION;
  EFI_SMBIOS_TABLE_HEADER *Header;

  UINTN Cores = 0;
  UINTN Threads = 0;
  while (TRUE) {
    Status = Smbios->GetNext(Smbios, &Handle, &Type, &Header, NULL);
    if (Status == EFI_NOT_FOUND) {
      break;
    } else if (EFI_ERROR(Status)) {
      CHECK_STATUS(Status);
    }

    SMBIOS_TABLE_TYPE4 *Info = (SMBIOS_TABLE_TYPE4 *) Header;

    // Print(L"Processor\n");
    // Print(L"  Type: %d\n", Info->ProcessorType);
    // Print(L"  Family: %d\n", Info->ProcessorFamily);
    // Print(L"  Id: %u\n", Info->ProcessorId);
    // Print(L"  External clock: %d\n", Info->ExternalClock);
    // Print(L"  Max speed: %d\n", Info->MaxSpeed);
    // Print(L"  Current speed: %d\n", Info->CurrentSpeed);
    // Print(L"  Status: %d\n", Info->Status);
    // Print(L"  Core count: %d\n", Info->CoreCount);
    // Print(L"  Enabled core count: %d\n", Info->EnabledCoreCount);
    // Print(L"  Thread count: %d\n", Info->ThreadCount);
    // Print(L"  Characteristics: %d\n", Info->ProcessorCharacteristics);

    Cores += Info->CoreCount;
    Threads += Info->ThreadCount;
  }

  *CoreCount = Cores;
  *ThreadCount = Threads;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI ProbeSystemConfigTable(void **AcpiTable, void **SmbiosTable) {
  UINTN Found = 0;
  EFI_GUID AcpiGuid = EFI_ACPI_TABLE_GUID;
  EFI_GUID SmbiosGuid = SMBIOS_TABLE_GUID;
  EFI_CONFIGURATION_TABLE *ConfigTable = gST->ConfigurationTable;
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE *Entry = &(ConfigTable[Index]);

    if (CompareGuid(&(Entry->VendorGuid), &AcpiGuid)) {
      *AcpiTable = Entry->VendorTable;
      Found++;
    } else if (CompareGuid(&(Entry->VendorGuid), &SmbiosGuid)) {
      *SmbiosTable = Entry->VendorTable;
      Found++;
    }

    if (Found == 2) {
      break;
    }
  }

  return EFI_SUCCESS;
}



//
// Protocol Initialization
//

EFI_STATUS EFIAPI InitMpServicesProtocol() {
  EFI_STATUS Status;
  EFI_GUID MpServicesGuid = EFI_MP_SERVICES_PROTOCOL_GUID;

  UINTN NumHandles;
  EFI_HANDLE *MpHandles;
  Status = gBS->LocateHandleBuffer(ByProtocol, &MpServicesGuid, NULL, &NumHandles, &MpHandles);
  CHECK_STATUS(Status);
  ASSERT(NumHandles > 0);

  EFI_HANDLE_PROTOCOL HandleProtocol = gST->BootServices->HandleProtocol;
  Status = HandleProtocol(MpHandles[0], &MpServicesGuid, (void **) &MpServices);
  CHECK_STATUS(Status);

  FreePool(MpHandles);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI InitSmbiosProtocol() {
  EFI_STATUS Status;
  EFI_GUID SmbiosGuid = EFI_SMBIOS_PROTOCOL_GUID;

  UINTN NumHandles;
  EFI_HANDLE *SmHandles;
  Status = gBS->LocateHandleBuffer(ByProtocol, &SmbiosGuid, NULL, &NumHandles, &SmHandles);
  CHECK_STATUS(Status);
  ASSERT(NumHandles > 0);

  EFI_HANDLE_PROTOCOL HandleProtocol = gST->BootServices->HandleProtocol;
  Status = HandleProtocol(SmHandles[0], &SmbiosGuid, (void **) &Smbios);
  CHECK_STATUS(Status);

  FreePool(SmHandles);
  return EFI_SUCCESS;
}

//
// Main
//

EFI_STATUS EFI_MAIN UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  DebugPrintEnabled();

  EFI_STATUS Status;
  CHAR16 *FileName;
  EFI_FILE_HANDLE File;

  EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

  EFI_HANDLE_PROTOCOL HandleProtocol = SystemTable->BootServices->HandleProtocol;
  Status = HandleProtocol(ImageHandle, &LoadedImageGuid, (void **) &LoadedImage);
  CHECK_STATUS(Status);
  Status = HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **) &FileSystem);
  CHECK_STATUS(Status);
  Status = HandleProtocol(LoadedImage->DeviceHandle, &gEfiDevicePathProtocolGuid, (void **) &RootPath);
  CHECK_STATUS(Status);
  Status = InitMpServicesProtocol();
  CHECK_STATUS(Status);
  Status = InitSmbiosProtocol();
  CHECK_STATUS(Status);
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  CHECK_STATUS(Status);

  /* -------- Config File Loading -------- */

  CHAR16 *ConfigFileName = L"/EFI/boot/config.ini";
  EFI_FILE_HANDLE ConfigFile;
  Status = LocateFile(Root, ConfigFileName, &ConfigFile);
  if (EFI_ERROR(Status)) {
    FileName = DefaultKernelFile;
    goto LoadKernel;
  }

  Print(L"[Loader] Loading config file\n");

  UINTN ConfigBufferSize;
  Status = LoadFile(ConfigFile, &ConfigBufferSize, &ConfigBuffer);
  CHECK_STATUS(Status);
  if (!EFI_ERROR(Status)) {
    Status = ConfigParse(ConfigBuffer, ConfigBufferSize);
  }

  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to load config file\n");
    FileName = DefaultKernelFile;
    goto LoadKernel;
  }

  Print(L"[Loader] Config file loaded\n");

  // get kernel file from config
  CHAR8 *AsciiFileName = ConfigGet("kernel");
  if (AsciiFileName != NULL) {
    UINTN StrLen = AsciiStrLen(AsciiFileName);
    FileName = AllocatePool(StrLen * sizeof(CHAR16) + 1);
    CHECK_NULL(FileName);

    AsciiStrToUnicodeStrS(AsciiFileName, FileName, StrLen + 1);
  } else {
    FileName = DefaultKernelFile;
  }

  /* -------- Video Mode -------- */

  UINT32 X;
  UINT32 Y;
  Status = ConfigGetDimensions("video", &X, &Y);
  if (!EFI_ERROR(Status)) {
    Print(L"[Loader] Setting video mode to %dx%d\n", X, Y);

    EFI_HANDLE GfxHandle;
    Status = VideoSet(X, Y, &GfxHandle);
    if (EFI_ERROR(Status)) {
      if (Status == EFI_UNSUPPORTED) {
        ErrorPrint(L"[Loader] Requested video mode %dx%d is not supported\n", X, Y);
      } else {
        CHECK_STATUS(Status);
      }
    }
  } else if (Status == EFI_UNSUPPORTED) {
    ErrorPrint(L"[Loader] Bad value for key 'video'\n");
  }

  /* -------- Kernel Loading -------- */

  label(LoadKernel)
  Status = LocateFile(Root, FileName, &File);
  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to locate file %s\n", FileName);
    CHECK_STATUS(Status);
  }

  UINTN KernelBufferSize;
  Status = LoadFile(File, &KernelBufferSize, &KernelBuffer);
  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to load file %s\n", FileName);
    CHECK_STATUS(Status);
  }

  Print(L"[Loader] File loaded\n");

  // Read Kernel Elf
  UINTN KernelSize;
  UINT64 KernelEntry;
  UINT64 KernelPhysAddr;
  UINT64 KernelVirtAddr;
  PAGE_DESCRIPTOR *KernelPages;
  Status = ReadElf(
    KernelBuffer,
    &KernelSize,
    &KernelEntry,
    &KernelPhysAddr,
    &KernelVirtAddr,
    &KernelPages
  );
  CHECK_STATUS(Status);

  // Memory Map

  EFI_MEMORY_MAP Mmap;
  Status = GetMemoryMap(&Mmap);
  CHECK_STATUS(Status);

  memory_map_t *KernelMmap;
  Status = CreateKernelMemoryMap(&Mmap, &KernelMmap);
  CHECK_STATUS(Status);

  UINTN CoreCount, ThreadCount;
  Status = SmbiosGetProcessorCount(&CoreCount, &ThreadCount);
  CHECK_STATUS(Status);

  /* -------------------------------------- */
  /*            Reserved Regions            */
  /* -------------------------------------- */
  UINTN KernelSizeAligned = ALIGN_VALUE(KernelSize, EFI_PAGE_SIZE);
  UINTN InfoSize = ALIGN_VALUE(sizeof(boot_info_t) + sizeof(memory_map_t) +
                               KernelMmap->mmap_capacity, EFI_PAGE_SIZE);
  UINTN PageTablesSize = EFI_PAGES_TO_SIZE(RESERVED_TABLES);

  // Kernel Region
  UINTN KernelRegionSize = KernelSizeAligned + InfoSize + PageTablesSize;
  // Reserved Region
  UINTN ReservedRegionSize = KERNEL_RESERVED - KernelRegionSize;
  // Stack Region
  UINTN LogicalCores = CoreCount * ThreadCount;
  UINTN StackRegionSize = LogicalCores * (STACK_SIZE + EFI_PAGE_SIZE);

  Print(L"[Loader] Kernel size: %u (%d pages)\n", KernelSizeAligned, EFI_SIZE_TO_PAGES(KernelSizeAligned));
  Print(L"[Loader] Boot info size: %u (%d pages)\n", InfoSize, EFI_SIZE_TO_PAGES(InfoSize));
  Print(L"[Loader] Page tables size: %u (%d pages)\n", PageTablesSize, EFI_SIZE_TO_PAGES(PageTablesSize));
  Print(L"[Loader] Reserved size: %u (%d pages)\n", ReservedRegionSize, EFI_SIZE_TO_PAGES(ReservedRegionSize));

  /* --------- Kernel Region --------- */
  UINT64 KernelAddress = KERNEL_PA;
  Status = LocateMemoryRegion(
    AtAddress,
    KernelMmap,
    KernelRegionSize,
    &KernelAddress
  );
  CHECK_STATUS(Status);

  // Create page descriptors for the info and page tables
  PAGE_DESCRIPTOR *InfoPages = MakePageDescriptor(
    KERNEL_OFFSET + KernelAddress + KernelSizeAligned,
    KernelAddress + KernelSizeAligned,
    EFI_SIZE_TO_PAGES(InfoSize + PageTablesSize),
    0b11 // Present | Read/Write
  );
  CHECK_NULL(InfoPages);
  AddPageDescriptor(KernelPages, InfoPages);

  /* --------- Kernel Reserved Region --------- */
  UINT64 ReservedAddress = KERNEL_PA + KernelRegionSize;
  Status = LocateMemoryRegion(
    AtAddress,
    KernelMmap,
    ReservedRegionSize,
    &ReservedAddress
  );
  CHECK_STATUS(Status);

  // Since the reserved region is a lot larger try to use
  // 2 MiB pages if possible
  UINT64 ReservedPtr = ReservedAddress;
  UINTN NumReservedPages = EFI_SIZE_TO_PAGES(ReservedRegionSize);
  while (NumReservedPages > 0) {
    UINT64 Next2MbBoundary = ALIGN_VALUE(ReservedPtr, SIZE_2MB);
    BOOLEAN Is2MbAligned = ReservedPtr == Next2MbBoundary;

    PAGE_DESCRIPTOR *ReservedPages;
    if (Is2MbAligned && NumReservedPages >= TABLE_LENGTH) {
      UINTN NumPages = NumReservedPages / TABLE_LENGTH;
      // Make a 2MB Page
      ReservedPages = MakePageDescriptor(
        KERNEL_OFFSET + ReservedPtr,
        ReservedPtr,
        NumPages,
        0b10000011 // Present | Read/Write | Page Size
      );

      ReservedPtr += NumPages * SIZE_2MB;
      NumReservedPages -= NumPages * TABLE_LENGTH;
    } else {
      // Make 4MB Pages
      UINTN NumPages = EFI_SIZE_TO_PAGES(Next2MbBoundary - ReservedPtr);
      ReservedPages = MakePageDescriptor(
        KERNEL_OFFSET + ReservedPtr,
        ReservedPtr,
        NumPages,
        0b11 // Present | Read/Write
      );

      ReservedPtr += EFI_PAGES_TO_SIZE(NumPages);
      NumReservedPages -= NumPages;
    }

    CHECK_NULL(ReservedPages);
    AddPageDescriptor(KernelPages, ReservedPages);
  }

  /* -------- Kernel Stack Region -------- */
  Print(L"[Loader] Allocating %d stack spaces\n", LogicalCores);

  UINT64 StackAddressBase;
  Status = LocateMemoryRegion(
    HighestAddress,
    KernelMmap,
    StackRegionSize,
    &StackAddressBase
  );
  CHECK_STATUS(Status);
  UINT64 StackVirtualTop = STACK_VA - 1;
  UINT64 StackVirtualBase = STACK_VA - StackRegionSize;

  PAGE_DESCRIPTOR *StackPages = NULL;
  PAGE_DESCRIPTOR *StackPagesLast = NULL;
  for (UINTN Index = 0; Index < LogicalCores; Index++) {
    UINT64 Offset = Index * (EFI_PAGE_SIZE + STACK_SIZE);

    // Since we're allocating the stack from the bottom up,
    // the stack boundary comes before the stack itself.

    // Stack boundary page descriptor
    PAGE_DESCRIPTOR *ZeroDesc = MakePageDescriptor(
      StackVirtualBase + Offset,
      StackAddressBase + Offset,
      1,
      0 // Not Present
    );
    CHECK_NULL(ZeroDesc);

    // Stack page descriptor
    PAGE_DESCRIPTOR *StackDesc = MakePageDescriptor(
      StackVirtualBase + Offset + EFI_PAGE_SIZE,
      StackAddressBase + Offset + EFI_PAGE_SIZE,
      EFI_SIZE_TO_PAGES(STACK_SIZE),
      0b11 // Present | Read/Write
    );
    CHECK_NULL(StackDesc);

    ZeroDesc->Next = StackDesc;
    if (StackPages == NULL) {
      StackPages = ZeroDesc;
    } else {
      StackPagesLast->Next = ZeroDesc;
    }
    StackPagesLast = StackDesc;
  }

  AddPageDescriptor(KernelPages, StackPages);

  /* ----------------------------------- */

  // PrintPageDescriptors(KernelPages);
  // PrintKernelMemoryMap(KernelMmap);

  UINT64 BootInfoAddress = KernelAddress + KernelSizeAligned;
  UINT64 MemoryMapAddress = BootInfoAddress + sizeof(boot_info_t);
  UINT64 PML4Address = BootInfoAddress + InfoSize;

  // Print(L"Boot info address: 0x%p\n", BootInfoAddress);
  // Print(L"Memory map address: 0x%p\n", MemoryMapAddress);
  // Print(L"PML4 address: 0x%p\n", PML4Address);
  // Print(L"Reserved address: 0x%p\n", ReservedAddress);

  // Get system info table pointers
  void *AcpiTable = NULL;
  void *SmbiosTable = NULL;
  Status = ProbeSystemConfigTable(&AcpiTable, &SmbiosTable);
  CHECK_STATUS(Status);

  Print(L"[Loader] Exiting boot services\n");
  gBS->ExitBootServices(ImageHandle, Mmap.MapKey);
  PostExitBootServices = TRUE;
  Print(L"[Loader] Done!\n");

  // Zero the whole region
  SetMem((void *) KernelAddress, KernelRegionSize, 0);

  // Load the kernel code
  LoadElf(KernelBuffer, KernelAddress);
  CreatePageTables(PML4Address, KernelPages);

  // Switch to the new page tables
  Print(L"[Loader] Switching page tables\n");
  DisableInterrupts();
  AsmWriteCr3((UINTN) PML4Address);
  EnableInterrupts();
  Print(L"[Loader] Done!\n");

  // Move the memory map to a better spot
  memory_map_t *MemoryMap = (memory_map_t *) (KERNEL_OFFSET + MemoryMapAddress);
  memory_region_t *Regions = (memory_region_t *) (KERNEL_OFFSET +
    MemoryMapAddress + sizeof(memory_map_t));
  CopyMem(Regions, KernelMmap->mmap, KernelMmap->mmap_capacity);
  MemoryMap->mem_total = KernelMmap->mem_total;
  MemoryMap->mmap_size = KernelMmap->mmap_size;
  MemoryMap->mmap_capacity = KernelMmap->mmap_capacity;
  MemoryMap->mmap = Regions;

  // Populate the boot_info_t struct
  boot_info_t *BootInfo = (boot_info_t *) (KERNEL_OFFSET + BootInfoAddress);
  BootInfo->magic[0] = BOOT_MAGIC0;
  BootInfo->magic[1] = BOOT_MAGIC1;
  BootInfo->magic[2] = BOOT_MAGIC2;
  BootInfo->magic[3] = BOOT_MAGIC3;

  BootInfo->kernel_phys = KernelAddress;
  BootInfo->num_cores = CoreCount;
  BootInfo->num_threads = ThreadCount;

  BootInfo->mem_map = MemoryMap;
  BootInfo->pml4 = KERNEL_OFFSET + PML4Address;
  BootInfo->reserved_base = KERNEL_OFFSET + ReservedAddress;
  BootInfo->reserved_size = ReservedRegionSize;

  BootInfo->fb_base = Graphics->Mode->FrameBufferBase;
  BootInfo->fb_size = Graphics->Mode->FrameBufferSize;
  BootInfo->fb_width = Graphics->Mode->Info->HorizontalResolution;
  BootInfo->fb_height = Graphics->Mode->Info->VerticalResolution;
  BootInfo->fb_pixels_per_scanline = Graphics->Mode->Info->PixelsPerScanLine;
  BootInfo->fb_pixel_format = Graphics->Mode->Info->PixelFormat;
  BootInfo->fb_pixel_info = *((pixel_bitmask_t *) &Graphics->Mode->Info->PixelInformation);

  BootInfo->runtime_services = (UINT64) gST->RuntimeServices;
  BootInfo->acpi_table = (UINT64) AcpiTable;
  BootInfo->smbios_table = (UINT64) SmbiosTable;

  //

  Print(L"[Loader] Kernel entry: 0x%p\n", KernelEntry);
  Print(L"[Loader] Kernel stack top: 0x%p\n", StackVirtualTop);
  KERNEL_ENTRY Entry = (KERNEL_ENTRY) KernelEntry;

  Print(L"[Loader] Loading kernel\n");
  Entry(StackVirtualTop, (UINT64) BootInfo);

  // We should not get here
  ErrorPrint(L"[Loader] Fatal Error\n");
  return EFI_ABORTED;
}
