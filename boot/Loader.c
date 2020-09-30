#include <boot.h>
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
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#include <Config.h>
#include <Memory.h>
#include <Video.h>

#include <Elf64.h>
#include <ElfCommon.h>

#define KERNEL_STACK_SIZE 0x8000
#define MAX_INFO_SIZE 1024

#define CheckStatus(Status) \
  if (EFI_ERROR(Status)){ \
    ErrorPrint(L"[Loader] Error code %d\n", Status); \
    FreePool(ConfigBuffer); \
    FreePool(KernelBuffer); \
    return Status; \
  } \
  NULL

typedef __attribute__((sysv_abi)) void (*KERNEL_ENTRY)(UINT64 StackPtr, boot_info_t *BootInfo);

BOOLEAN DidExitBootServices = FALSE;

EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
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
  if (FileInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

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

      DEBUG((EFI_D_INFO, "[Loader] Entering new directory\n"));
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
  if (FileInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

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

EFI_STATUS EFIAPI FindAvailableRegion(
  IN memory_map_t *MemoryMap,
  IN UINTN MemoryMapSize,
  IN UINTN RegionSize,
  OUT UINT64 *Address
) {
  memory_map_t *Region = NULL;

  memory_map_t *Ptr = MemoryMap;
  while ((UINTN) Ptr < (UINTN) MemoryMap + MemoryMapSize) {
    if (Ptr->type == MEMORY_FREE) {
      if (!Region || (Ptr->phys_addr > Region->phys_addr && Ptr->size >= RegionSize)) {
        Region = Ptr;
      }
    }
    Ptr++;
  }

  if (Region == NULL) {
    return EFI_NOT_FOUND;
  }

  Print(L"Found a region\n");
  Print(L"Region base: 0x%p\n", Region->phys_addr);
  Print(L"Region size: %u\n", Region->size);

  // If the region is larger than we need, we can use
  // the upper part of the region for the kernel and
  // leave the rest available.
  UINTN Offset = Region->size - RegionSize;
  *Address = Region->phys_addr + Offset;
  Region->type = MEMORY_RESERVED;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LoadElf(void *ElfImage, UINT64 Address, UINT64 *Entry) {
  ASSERT(DidExitBootServices);

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

  Print(L"Kernel Image: 0x%p\n", ElfImage);
  Print(L"0x%p\n", ElfHdr->e_entry);

  Print(L"ELF Header:\n");
  Print(L"  Magic: 0x7F 0x45 0x4C 0x46\n");
  Print(L"  Class: ELF64 (%d)\n", ElfHdr->e_ident[EI_CLASS]);
  Print(L"  Data: %d\n", ElfHdr->e_ident[EI_DATA]);
  Print(L"  Version: %d\n", ElfHdr->e_ident[EI_VERSION]);
  Print(L"  OS/ABI: %d\n", ElfHdr->e_ident[EI_OSABI]);
  Print(L"  ABI Version: %d\n", ElfHdr->e_ident[EI_ABIVERSION]);
  Print(L"  Type: %d\n", ElfHdr->e_type);
  Print(L"  Machine: %d\n", ElfHdr->e_machine);
  Print(L"  Entry point address: 0x%p\n", ElfHdr->e_entry);
  Print(L"  Start of program headers: %u\n", ElfHdr->e_phoff);
  Print(L"  Start of section headers: %u\n", ElfHdr->e_shoff);
  Print(L"  Flags: 0x%x\n", ElfHdr->e_flags);
  Print(L"  Size of this header: %u\n", ElfHdr->e_ehsize);
  Print(L"  Size of program headers: %u\n", ElfHdr->e_phentsize);
  Print(L"  Number of program headers: %d\n", ElfHdr->e_phnum);
  Print(L"  Size of section headers: %u\n", ElfHdr->e_shentsize);
  Print(L"  Number of section headers: %d\n", ElfHdr->e_shnum);
  Print(L"  Section header string table index: %u\n", ElfHdr->e_shstrndx);


  Elf64_Phdr *ProgramHdr = ElfImage + ElfHdr->e_phoff;
  for (Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (ProgramHdr->p_type == PT_LOAD) {
      void *FileSegment;
      void *MemSegment;
      void *Zeros;
      UINTN ZerosCount;

      FileSegment = (void *) ((UINTN) ElfImage + ProgramHdr->p_offset);
      MemSegment = (void *) Address;

      Print(L"[Loader] Copying kernel from 0x%p to 0x%p\n", FileSegment, MemSegment);
      CopyMem(MemSegment, FileSegment, ProgramHdr->p_filesz);

      Zeros = (UINT8 *) MemSegment + ProgramHdr->p_filesz;
      ZerosCount = ProgramHdr->p_memsz - ProgramHdr->p_filesz;
      if (ZerosCount > 0) {
        SetMem(Zeros, ZerosCount, 0);
      }

      ProgramHdr = (Elf64_Phdr *) ((UINTN) ProgramHdr + ElfHdr->e_phentsize);
    }

  }

  // CpuBreakpoint();

  *Entry = ElfHdr->e_entry;
  return EFI_SUCCESS;
}

//
// Bootloader Main
//

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  DebugPrintEnabled();

  EFI_STATUS Status;
  CHAR16 *FileName;
  EFI_FILE_HANDLE File;

  EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

  EFI_HANDLE_PROTOCOL HandleProtocol = SystemTable->BootServices->HandleProtocol;
  Status = HandleProtocol(ImageHandle, &LoadedImageGuid, (void **) &LoadedImage);
  CheckStatus(Status);
  Status = HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **) &FileSystem);
  CheckStatus(Status);
  Status = HandleProtocol(LoadedImage->DeviceHandle, &gEfiDevicePathProtocolGuid, (void **) &RootPath);
  CheckStatus(Status);
  Status = FileSystem->OpenVolume(FileSystem, &Root);
  CheckStatus(Status);

  //
  // Config File Loading
  //

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
  CheckStatus(Status);
  if (!EFI_ERROR(Status)) {
    Status = ConfigParse(ConfigBuffer, ConfigBufferSize);
  }

  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to load config file\n");
    FileName = DefaultKernelFile;
    goto LoadKernel;
  }

  Print(L"[Loader] Config file loaded!\n");

  // get kernel file from config
  CHAR8 *AsciiFileName = ConfigGet("kernel");
  if (AsciiFileName != NULL) {
    UINTN StrLen = AsciiStrLen(AsciiFileName);
    FileName = AllocatePool(StrLen * sizeof(CHAR16) + 1);
    if (FileName == NULL) {
      CheckStatus(EFI_OUT_OF_RESOURCES);
    }

    AsciiStrToUnicodeStrS(AsciiFileName, FileName, StrLen + 1);
  } else {
    FileName = DefaultKernelFile;
  }

  //
  // Video Mode
  //

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
        CheckStatus(Status);
      }
    }
  } else if (Status == EFI_UNSUPPORTED) {
    ErrorPrint(L"[Loader] Bad value for key 'video'\n");
  }

  //
  // Kernel Loading
  //

LoadKernel:
  Status = LocateFile(Root, FileName, &File);
  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to locate kernel\n");
    CheckStatus(Status);
  }

  Print(L"[Loader] Loading kernel %s\n", FileName);

  UINTN KernelBufferSize;
  Status = LoadFile(File, &KernelBufferSize, &KernelBuffer);
  if (EFI_ERROR(Status)) {
    ErrorPrint(L"[Loader] Failed to load kernel\n");
    CheckStatus(Status);
  }

  Print(L"[Loader] File loaded!\n");

  EFI_MEMORY_MAP Mmap;
  Status = GetMemoryMap(&Mmap);
  CheckStatus(Status);

  memory_map_t *KernelMmap;
  UINTN KernelMmapSize;
  UINTN TotalMemory;
  Status = CreateKernelMemoryMap(&Mmap, &KernelMmap, &KernelMmapSize, &TotalMemory);
  CheckStatus(Status);

  UINTN KernelSizeAligned = ALIGN_VALUE(KernelBufferSize, EFI_PAGE_SIZE);
  UINTN InfoSizeAligned = ALIGN_VALUE(sizeof(boot_info_t) + KernelMmapSize, EFI_PAGE_SIZE);

  // <---- Bottom of available region
  // Kernel
  // Boot info
  // Memory map
  // Page table - PML4
  // Page table - PDPT
  // Page table - PDT
  // Page table - PT
  // Zero page (overflow guard)
  //     ...
  // Kernel stack (grows down)
  // <---- Top of available region
  UINTN RegionSize = KernelSizeAligned + InfoSizeAligned +
                     EFI_PAGES_TO_SIZE(5) + KERNEL_STACK_SIZE;

  Print(L"[Loader] Kernel size: %u\n", KernelBufferSize);
  Print(L"[Loader] Kernel region size: %u\n", RegionSize);

  // Find available region to load the kernel in once
  // we enter runtime. Note that we're looking through
  // the kernel memory map because a region may be
  // currently used for boot services code/data that
  // will be usuable after exiting boot services.
  UINT64 RegionAddress;
  Status = FindAvailableRegion(KernelMmap, KernelMmapSize, RegionSize, &RegionAddress);
  CheckStatus(Status);

  UINT64 BootInfoAddress = RegionAddress + KernelSizeAligned;
  UINT64 MemoryMapAddress = BootInfoAddress + sizeof(boot_info_t);
  UINT64 PML4Address = RegionAddress + KernelSizeAligned + InfoSizeAligned;
  UINT64 StackAddress = RegionAddress + RegionSize;

  Print(L"[Loader] Kernel is at: 0x%p\n", RegionAddress);
  Print(L"[Loader] Boot info is at: 0x%p\n", BootInfoAddress);
  Print(L"[Loader] PML4 is at: 0x%p\n", PML4Address);
  Print(L"[Loader] Stack is at: 0x%p\n", StackAddress);

  Print(L"[Loader] Exiting boot services\n");
  gBS->ExitBootServices(ImageHandle, Mmap.MapKey);
  DidExitBootServices = TRUE;
  Print(L"[Loader] Done!\n");

  // Zero the whole region
  SetMem((void *) RegionAddress, RegionSize, 0);

  // Load the kernel code
  UINT64 KernelEntry;
  LoadElf(KernelBuffer, RegionAddress, &KernelEntry);
  CreatePageTables(RegionAddress, RegionSize, PML4Address);

  // Switch to the new page tables
  Print(L"[Loader] Switching page tables\n");
  DisableInterrupts();
  AsmWriteCr3((UINTN) PML4Address);
  EnableInterrupts();
  Print(L"[Loader] Done!\n");

  // Move the memory map to a better spot
  memory_map_t *MemoryMap = (memory_map_t *) MemoryMapAddress;
  CopyMem(MemoryMap, KernelMmap, KernelMmapSize);

  // Create the boot_info_t struct
  boot_info_t *BootInfo = (boot_info_t *) BootInfoAddress;


  BootInfo->magic[0] = BOOT_MAGIC0;
  BootInfo->magic[1] = BOOT_MAGIC1;
  BootInfo->magic[2] = BOOT_MAGIC2;
  BootInfo->magic[3] = BOOT_MAGIC3;

  BootInfo->mem_total = TotalMemory;
  BootInfo->mmap = MemoryMap;
  BootInfo->mmap_size = KernelMmapSize;

  BootInfo->fb_ptr = Graphics->Mode->FrameBufferBase;
  BootInfo->fb_size = Graphics->Mode->FrameBufferSize;
  BootInfo->fb_width = Graphics->Mode->Info->HorizontalResolution;
  BootInfo->fb_height = Graphics->Mode->Info->VerticalResolution;
  BootInfo->fb_pps = Graphics->Mode->Info->PixelsPerScanLine;

  BootInfo->rt = gST->RuntimeServices;

  //

  KERNEL_ENTRY Entry = (KERNEL_ENTRY) KernelEntry;

  Print(L"[Loader] Starting kernel\n");
  Entry(StackAddress, BootInfo);

  // We should not get here
  ErrorPrint(L"[Loader] Fatal Error\n");
  return EFI_ABORTED;
}
