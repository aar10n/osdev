//
// Created by Aaron Gill-Braun on 2022-05-30.
//

#include <Loader.h>
#include <File.h>

#include <elf.h>
#include <elf64.h>

#define EHDR_OFFSET(ehdr, offset) ((VOID *)((UINTN)(ehdr) + (offset)))
#define NEXT_PHDR(ehdr, phdr) ((Elf64_Phdr *)((UINTN)(phdr) + (ehdr)->e_phentsize))

BOOLEAN ElfVerifyHeader(IN Elf64_Ehdr *ElfHdr) {
  UINT8 IdentMagic[4] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};
  for (UINTN Index = 0; Index < 4; Index++) {
    if (ElfHdr->e_ident[Index] != IdentMagic[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

UINTN ElfGetTotalLoadSize(IN Elf64_Ehdr *ElfHdr) {
  Elf64_Phdr *Phdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_phoff);
  UINTN TotalSize = 0;

  for (UINTN Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (Phdr->p_type == PT_LOAD) {
      TotalSize += Phdr->p_memsz;
    }
    Phdr = NEXT_PHDR(ElfHdr, Phdr);
  }
  return TotalSize;
}

//

EFI_STATUS EFIAPI ReadElf(IN VOID *Buffer, OUT UINT64 *EntryPoint, OUT UINTN *MemSize) {
  Elf64_Ehdr *ElfHdr = Buffer;
  if (!ElfVerifyHeader(ElfHdr)) {
    PRINT_ERROR("Invalid ELF file");
    return EFI_INVALID_PARAMETER;
  }

  // make sure the ELF file is the correct type
  if (ElfHdr->e_ident[EI_CLASS] != ELFCLASS64) {
    PRINT_ERROR("Unsupported ELF file type");
    return EFI_UNSUPPORTED;
  } else if (ElfHdr->e_ident[EI_OSABI] != ELFOSABI_SYSV) {
    PRINT_ERROR("Unsupported ELF OS/ABI");
    return EFI_UNSUPPORTED;
  }

  // get size of ELF file when loaded into memory
  UINTN LoadedSize = ElfGetTotalLoadSize(ElfHdr);
  *EntryPoint = ElfHdr->e_entry;
  *MemSize = LoadedSize;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LoadElf(IN VOID *Buffer, IN UINT64 PhysAddr, OUT MEMORY_DESCRIPTOR **Pages) {
  Elf64_Ehdr *ElfHdr = Buffer;
  if (!ElfVerifyHeader(ElfHdr)) {
    PRINT_ERROR("Invalid ELF file");
    return EFI_INVALID_PARAMETER;
  }

  // load the ELF segments
  MEMORY_DESCRIPTOR *Desc = NULL;
  Elf64_Phdr *ProgramHdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_phoff);
  for (UINTN Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (ProgramHdr->p_type == PT_NULL) {
      goto NEXT;
    } else if (ProgramHdr->p_type != PT_LOAD) {
      PRINT_WARN("Unsupported program header in ELF file (%d)", ProgramHdr->p_type);
      goto NEXT;
    }

    // PT_LOAD segment
    BOOLEAN First = Desc == NULL;
    Desc = NewDescriptor(
      Desc,
      EfiConventionalMemory,
      ProgramHdr->p_vaddr,
      PhysAddr,
      EFI_SIZE_TO_PAGES(ProgramHdr->p_memsz),
      0
    );
    if (First) {
      *Pages = Desc;
    }

    CopyMem((VOID *) PhysAddr, EHDR_OFFSET(ElfHdr, ProgramHdr->p_offset), ProgramHdr->p_filesz);
    ZeroMem((VOID *) (PhysAddr + ProgramHdr->p_filesz), ProgramHdr->p_memsz - ProgramHdr->p_filesz);
    PhysAddr += ProgramHdr->p_memsz;

  NEXT:
    ProgramHdr = NEXT_PHDR(ElfHdr, ProgramHdr);
  }

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LoadKernel(
  IN CONST CHAR16 *Path,
  IN UINT64 PhysAddr,
  OUT UINT64 *Entry,
  OUT MEMORY_DESCRIPTOR **Pages
) {
  EFI_STATUS Status;

  PRINT_INFO("Loading kernel...");
  EFI_FILE *KernelImageHandle;
  Status = OpenFile(Path, &KernelImageHandle);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to open kernel image");
    return Status;
  }

  UINTN KernelImageSize;
  VOID *KernelImageBuffer;
  Status = ReadFile(KernelImageHandle, &KernelImageSize, &KernelImageBuffer);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to read kernel image");
    return Status;
  }

  PRINT_INFO("Kernel image size: %d", KernelImageSize);

  UINT64 KernelEntry;
  UINTN MemSize;
  Status = ReadElf(KernelImageBuffer, &KernelEntry, &MemSize);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Bad kernel image format");
    return Status;
  }

  PRINT_INFO("Kernel entry point: %d", KernelEntry);
  PRINT_INFO("Kernel memory size: %d", MemSize);

  // load elf segments
  MEMORY_DESCRIPTOR *KernelPages = NULL;
  Status = LoadElf(KernelImageBuffer, PhysAddr, &KernelPages);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to load kernel image");
    return Status;
  }

  PRINT_INFO("Kernel entry point: 0x%p", KernelEntry);

  *Entry = KernelEntry;
  *Pages = KernelPages;

  PRINT_INFO("Kernel loaded!");
  return EFI_SUCCESS;
}

//
// Debugging
//

EFI_STATUS EFIAPI PrintElfHeader(IN VOID *Buffer) {
  Elf64_Ehdr *ElfHdr = (Elf64_Ehdr *) Buffer;
  if (!ElfVerifyHeader(ElfHdr)) {
    PRINT_ERROR("Invalid ELF file");
    return EFI_INVALID_PARAMETER;
  }

  PRINT_INFO("ELF Header:");
  PRINT_INFO("    Magic: 0x7F 0x45 0x4C 0x46");
  PRINT_INFO("    Class: ELF64 (%d)", ElfHdr->e_ident[EI_CLASS]);
  PRINT_INFO("    Data: %d", ElfHdr->e_ident[EI_DATA]);
  PRINT_INFO("    Version: %d", ElfHdr->e_ident[EI_VERSION]);
  PRINT_INFO("    OS/ABI: %d", ElfHdr->e_ident[EI_OSABI]);
  PRINT_INFO("    ABI Version: %d", ElfHdr->e_ident[EI_ABIVERSION]);
  PRINT_INFO("    Type: %d", ElfHdr->e_type);
  PRINT_INFO("    Machine: %d", ElfHdr->e_machine);
  PRINT_INFO("    Entry point address: 0x%p", ElfHdr->e_entry);
  PRINT_INFO("    Start of program headers: %u", ElfHdr->e_phoff);
  PRINT_INFO("    Start of section headers: %u", ElfHdr->e_shoff);
  PRINT_INFO("    Flags: 0x%x", ElfHdr->e_flags);
  PRINT_INFO("    Size of this header: %u", ElfHdr->e_ehsize);
  PRINT_INFO("    Size of program headers: %u", ElfHdr->e_phentsize);
  PRINT_INFO("    Number of program headers: %d", ElfHdr->e_phnum);
  PRINT_INFO("    Size of section headers: %u", ElfHdr->e_shentsize);
  PRINT_INFO("    Number of section headers: %d", ElfHdr->e_shnum);
  PRINT_INFO("    Section header string table index: %u", ElfHdr->e_shstrndx);
  return EFI_SUCCESS;
}
