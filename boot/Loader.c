//
// Created by Aaron Gill-Braun on 2022-05-30.
//

#include <Loader.h>
#include <File.h>

#include <elf.h>

#define EHDR_OFFSET(ehdr, offset) ((VOID *)((UINTN)(ehdr) + (offset)))
#define NEXT_PHDR(ehdr, phdr) ((Elf64_Phdr *)((UINTN)(phdr) + (ehdr)->e_phentsize))
#define NEXT_SHDR(ehdr, shdr) ((Elf64_Shdr *)((UINTN)(shdr) + (ehdr)->e_shentsize))
#define NEXT_SYM(shdr, sym) ((Elf64_Sym *)((UINTN)(sym) + (shdr)->sh_entsize))

// ------------------------------------------------
//   ELF Helper Functions
// ------------------------------------------------

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
    if (Phdr->p_type == PT_LOAD && Phdr->p_memsz > 0) {
      TotalSize += ALIGN_VALUE(Phdr->p_memsz, EFI_PAGE_SIZE);
    }
    Phdr = NEXT_PHDR(ElfHdr, Phdr);
  }
  return TotalSize;
}

CHAR8 *ElfGetStringForSymbol(IN Elf64_Ehdr *ElfHdr, IN Elf64_Shdr *SymTabHdr, IN Elf64_Sym *Sym) {
  if (Sym->st_name == 0) {
    return NULL;
  }

  UINTN StrTabOffset = (UINTN) ElfHdr->e_shentsize * SymTabHdr->sh_link;
  Elf64_Shdr *StrTab = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff + StrTabOffset);
  return EHDR_OFFSET(ElfHdr, StrTab->sh_offset + Sym->st_name);
}

Elf64_Phdr EFIAPI *ElfLocateProgramHeaderByType(IN Elf64_Ehdr *ElfHdr, Elf64_Phdr *Last, IN Elf64_Word ProgramType) {
  Elf64_Phdr *Phdr = NULL;
  if (Last == NULL) {
    Phdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_phoff);
  } else {
    Phdr = NEXT_PHDR(ElfHdr, Last);
  }

  for (UINTN Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (Phdr->p_type == ProgramType) {
      return Phdr;
    }
    Phdr = NEXT_PHDR(ElfHdr, Phdr);
  }
  return NULL;
}

Elf64_Shdr EFIAPI *ElfLocateSectionHeaderByType(IN Elf64_Ehdr *ElfHdr, IN Elf64_Word SectionType) {
  Elf64_Shdr *Shdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff);
  for (UINTN Index = 0; Index < ElfHdr->e_shnum; Index++) {
    if (Shdr->sh_type == SectionType) {
      return Shdr;
    }
    Shdr = NEXT_SHDR(ElfHdr, Shdr);
  }

  return NULL;
}

VOID EFIAPI *ElfLocateSectionHeaderByName(IN Elf64_Ehdr *ElfHdr, IN CONST CHAR8 *SectionName) {
  Elf64_Shdr *Shdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff);
  Elf64_Shdr *ShStrTab = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff + (UINTN) ElfHdr->e_shentsize * ElfHdr->e_shstrndx);

  for (UINTN Index = 0; Index < ElfHdr->e_shnum; Index++) {
    if (Shdr->sh_name == 0) {
      Shdr = NEXT_SHDR(ElfHdr, Shdr);
      continue;
    }

    CHAR8 *String = EHDR_OFFSET(ElfHdr, ShStrTab->sh_offset + Shdr->sh_name);
    if (AsciiStrCmp(SectionName, String) == 0) {
      return Shdr;
    }
    Shdr = NEXT_SHDR(ElfHdr, Shdr);
  }
  return NULL;
}

Elf64_Sym EFIAPI *ElfLocateSymbolByName(IN Elf64_Ehdr *ElfHdr, IN CONST CHAR8 *SymbolName) {
  Elf64_Shdr *SymTab = ElfLocateSectionHeaderByType(ElfHdr, SHT_SYMTAB);
  if (SymTab == NULL) {
    return NULL;
  }

  UINTN StrTabOffset = (UINTN) ElfHdr->e_shentsize * SymTab->sh_link;
  Elf64_Shdr *StrTab = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff + StrTabOffset);
  Elf64_Sym *Sym = EHDR_OFFSET(ElfHdr, SymTab->sh_offset + SymTab->sh_entsize);
  UINTN NumSymbols = SymTab->sh_size / SymTab->sh_entsize;
  for (UINTN Index = 0; Index < NumSymbols; Index++) {
    if (Sym->st_name == 0) {
      Sym = NEXT_SYM(SymTab, Sym);
      continue;
    }

    CHAR8 *SymName = EHDR_OFFSET(ElfHdr, StrTab->sh_offset + Sym->st_name);
    if (AsciiStrCmp(SymbolName, SymName) == 0) {
      return Sym;
    }

    Sym = NEXT_SYM(SymTab, Sym);
  }

  return NULL;
}

Elf64_Sym EFIAPI *LocateObjectSymbolInSection(IN Elf64_Ehdr *ElfHdr, IN CONST CHAR8 *SectionName, IN CONST CHAR8 *SymbolName) {
  Elf64_Shdr *SymTabHdr = ElfLocateSectionHeaderByType(ElfHdr, SHT_SYMTAB);
  Elf64_Shdr *SectionHdr = ElfLocateSectionHeaderByName(ElfHdr, SectionName);
  if (SymTabHdr == NULL || SectionHdr == NULL || SectionHdr->sh_type != SHT_PROGBITS) {
    PRINT_WARN("LocateObjectSymbolInSection bad section");
    return NULL;
  }

  Elf64_Shdr *FirstShdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff);
  UINTN SectionIndex = SectionHdr - FirstShdr;
  Elf64_Sym *Sym = EHDR_OFFSET(ElfHdr, SymTabHdr->sh_offset + SymTabHdr->sh_entsize);
  UINTN NumSymbols = SymTabHdr->sh_size / SymTabHdr->sh_entsize;
  for (UINTN Index = 0; Index < NumSymbols; Index++) {
    if (Sym->st_shndx != SectionIndex) {
      Sym = NEXT_SYM(SymTabHdr, Sym);
      continue;
    }

    if (ELF64_ST_TYPE(Sym->st_info) == STT_OBJECT) {
      CHAR8 *SymName = ElfGetStringForSymbol(ElfHdr, SymTabHdr, Sym);
      if (SymName == NULL) {
        continue;
      }

      PRINT_INFO("===> found: %a, type = %d, bind = %d, size = %llu, value = 0x%p",
                 SymName, ELF64_ST_TYPE(Sym->st_info), ELF64_ST_BIND(Sym->st_info), Sym->st_size, Sym->st_value);
      if (AsciiStrCmp(SymbolName, SymName) == 0) {
        return Sym;
      }
    }

    Sym = NEXT_SYM(SymTabHdr, Sym);
  }

  return NULL;
}

Elf64_Sym EFIAPI *LocateKernelBootInfoSymbol(IN Elf64_Ehdr *ElfHdr) {
  Elf64_Shdr *SymTabHdr = ElfLocateSectionHeaderByType(ElfHdr, SHT_SYMTAB);
  Elf64_Shdr *BootDataHdr = ElfLocateSectionHeaderByName(ElfHdr, ".boot_data");
  if (SymTabHdr == NULL || BootDataHdr == NULL || BootDataHdr->sh_type != SHT_PROGBITS) {
    // no boot data section
    PRINT_WARN("No .boot_data section found");
    return NULL;
  }

  PRINT_INFO("Found .boot_data section");
  PRINT_INFO("Looking for boot info symbol");

  Elf64_Shdr *FirstShdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff);
  UINTN BootDataIndex = BootDataHdr - FirstShdr;
  Elf64_Sym *Sym = EHDR_OFFSET(ElfHdr, SymTabHdr->sh_offset + SymTabHdr->sh_entsize);
  UINTN NumSymbols = SymTabHdr->sh_size / SymTabHdr->sh_entsize;
  for (UINTN Index = 0; Index < NumSymbols; Index++) {
    if (Sym->st_shndx != BootDataIndex) {
      Sym = NEXT_SYM(SymTabHdr, Sym);
      continue;
    }

    if (Sym->st_info & STT_OBJECT) {
      CHAR8 *SymName = ElfGetStringForSymbol(ElfHdr, SymTabHdr, Sym);
      if (SymName != NULL) {
        PRINT_INFO("Found boot info symbol '%a'", SymName);
      } else {
        PRINT_INFO("Found boot info symbol in section: .boot_data");
      }
      return Sym;
    }

    PRINT_WARN("Found invalid non-object symbol in section: .boot_data");
    Sym = NEXT_SYM(SymTabHdr, Sym);
  }

  return NULL;
}

// ------------------------------------------------
//   ELF Loading Functions
// ------------------------------------------------

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

EFI_STATUS EFIAPI LoadElf(IN VOID *Buffer, IN UINT64 PhysAddr, OUT PAGE_DESCRIPTOR **Pages) {
  Elf64_Ehdr *ElfHdr = Buffer;
  if (!ElfVerifyHeader(ElfHdr)) {
    PRINT_ERROR("Invalid ELF file");
    return EFI_INVALID_PARAMETER;
  }

  // load the ELF segments
  PAGE_DESCRIPTOR *Desc = NULL;
  Elf64_Phdr *ProgramHdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_phoff);
  for (UINTN Index = 0; Index < ElfHdr->e_phnum; Index++) {
    if (ProgramHdr->p_type == PT_NULL) {
      goto NEXT;
    } else if (ProgramHdr->p_type != PT_LOAD) {
      PRINT_WARN("Unsupported program header in ELF file (%u)", ProgramHdr->p_type);
      goto NEXT;
    }

    UINTN FileSize = ProgramHdr->p_filesz;
    UINTN MemSize = ALIGN_VALUE(ProgramHdr->p_memsz, ProgramHdr->p_align);
    UINT32 Flags = 0;
    Flags |= ProgramHdr->p_flags & PF_W ? PD_WRITE : 0;
    Flags |= ProgramHdr->p_flags & PF_X ? PD_EXECUTE : 0;

    if (ProgramHdr->p_flags & PF_W && ProgramHdr->p_flags & PF_X) {
      PRINT_INFO("Loading executable data segment");
    } else if (ProgramHdr->p_flags & PF_W) {
      PRINT_INFO("Loading data segment");
    } else if (ProgramHdr->p_flags & PF_X) {
      PRINT_INFO("Loading code segment");
    } else {
      PRINT_INFO("Loading rodata segment");
    }
    PRINT_INFO("  base: 0x%p", ProgramHdr->p_vaddr);
    PRINT_INFO("  size: 0x%llx (%llu pages)", ProgramHdr->p_memsz, EFI_SIZE_TO_PAGES(MemSize));

    // PT_LOAD segment
    PAGE_DESCRIPTOR *OldDesc = Desc;
    Desc = NewDescriptor(
      Desc,
      PhysAddr,
      ProgramHdr->p_vaddr,
      EFI_SIZE_TO_PAGES(MemSize),
      Flags
    );
    if (OldDesc == NULL) {
      *Pages = Desc; // first kernel descriptor
    }

    CopyMem((VOID *) PhysAddr, EHDR_OFFSET(ElfHdr, ProgramHdr->p_offset), FileSize);
    ZeroMem((VOID *) (PhysAddr + FileSize), MemSize - FileSize);
    PhysAddr += MemSize;

  NEXT:
    ProgramHdr = NEXT_PHDR(ElfHdr, ProgramHdr);
  }

  return EFI_SUCCESS;
}

// ------------------------------------------------
//   Kernel Loading Functions
// ------------------------------------------------

EFI_STATUS EFIAPI LoadKernelRequestedSections(IN Elf64_Ehdr *ElfHdr, IN PAGE_DESCRIPTOR *Pages) {
  // load the requested sections immediate after the kernel code + data in physical memory
  PAGE_DESCRIPTOR *LastPage = GetLastDescriptor(Pages);
  ASSERT(LastPage != NULL);
  EFI_PHYSICAL_ADDRESS PhysAddr = LastPage->PhysAddr + EFI_PAGES_TO_SIZE(LastPage->NumPages);

  Elf64_Shdr *SymTabHdr = ElfLocateSectionHeaderByType(ElfHdr, SHT_SYMTAB);
  Elf64_Shdr *LoadSectionsHdr = ElfLocateSectionHeaderByName(ElfHdr, ".load_sections");
  if (SymTabHdr == NULL || LoadSectionsHdr == NULL || LoadSectionsHdr->sh_type != SHT_PROGBITS) {
    PRINT_WARN("No .load_sections section found");
    return EFI_SUCCESS;
  }

  PRINT_INFO("Found .load_sections section");

  // Iterate through all symbols in the '.load_sections' section. Each symbol should be
  // a variable defined by the LOAD_SECTION macro with the type 'loaded_section_t'. The
  // `name` field in the struct pointed to by each symbol will indicate the name of the
  // section being requested.
  //
  // If the section being requested is part of PT_LOAD segment, just point the struct to
  // the virtual address of where it was loaded. Otherwise, we load the requested section
  // into physical memory, and let the kernel map it into its own virtual memory later.
  //
  // Once the corresponding section has been mapped, we populate struct pointed to by each
  // symbol with the physical and virtual (if applicable) addresses, and size of the loaded
  // section.
  Elf64_Shdr *FirstShdr = EHDR_OFFSET(ElfHdr, ElfHdr->e_shoff);
  UINTN LoadSectionsIndex = LoadSectionsHdr - FirstShdr;
  Elf64_Sym *Sym = EHDR_OFFSET(ElfHdr, SymTabHdr->sh_offset + SymTabHdr->sh_entsize);
  UINTN NumSymbols = SymTabHdr->sh_size / SymTabHdr->sh_entsize;
  for (UINTN Index = 0; Index < NumSymbols; Index++) {
    if (Sym->st_shndx != LoadSectionsIndex) {
      goto NEXT;
    } else if (ELF64_ST_TYPE(Sym->st_info) != STT_OBJECT) {
      PRINT_WARN("Invalid symbol in .load_sections (non-object)");
      goto NEXT;
    } else if (Sym->st_size != sizeof(loaded_section_t)) {
      PRINT_WARN("Invalid symbol in .load_sections (size = %llu)", Sym->st_size);
      goto NEXT;
    }

    CHAR8 *SymName = ElfGetStringForSymbol(ElfHdr, SymTabHdr, Sym);
    if (SymName == NULL) {
      // is this possible at this point?
      goto NEXT;
    }

    // Since the kernel mappings have not been applied yet, we must convert the symbol's
    // virtual address into a physical address so we can read/write from the struct.
    UINT64 SymPhysAddr = ConvertVirtToPhysFromDescriptors(Pages, Sym->st_value);
    loaded_section_t *section = (VOID *) SymPhysAddr;
    // do the same for the section name string
    UINT64 SectionNamePhysAddr = ConvertVirtToPhysFromDescriptors(Pages, (UINT64) section->name);
    CHAR8 *SectionName = (VOID *) SectionNamePhysAddr;

    // now we can find and load the requested section
    PRINT_INFO("  loading section '%a' %a", SectionName, SymName);
    Elf64_Shdr *SectionHdr = ElfLocateSectionHeaderByName(ElfHdr, SectionName);
    if (SectionHdr == NULL) {
      PRINT_WARN("Failed to load section '%a', does not exist", SectionName);
      goto NEXT;
    }


    UINTN NumPages = EFI_SIZE_TO_PAGES(SectionHdr->sh_size);
    UINTN FileSize = SectionHdr->sh_size;
    UINTN MemSize = EFI_PAGES_TO_SIZE(NumPages);

    section->size = FileSize;
    if (SectionHdr->sh_addr != 0) {
      // this section was already loaded as part of the
      // normal elf loading process
      section->phys_addr = ConvertVirtToPhysFromDescriptors(Pages, SectionHdr->sh_addr);
      section->virt_addr = SectionHdr->sh_addr;
    } else {
      // load the elf section
      CopyMem((VOID *) PhysAddr, EHDR_OFFSET(ElfHdr, SectionHdr->sh_offset), FileSize);
      ZeroMem((VOID *) (PhysAddr + FileSize), MemSize - FileSize);

      section->phys_addr = PhysAddr;
      section->virt_addr = 0; // unmapped - leave for the kernel
      PhysAddr += MemSize;
    }

  NEXT:
    Sym = NEXT_SYM(SymTabHdr, Sym);
  }

  return EFI_SUCCESS;
}

//

EFI_STATUS EFIAPI LoadKernel(
  IN CONST CHAR16 *Path,
  IN UINT64 PhysAddr,
  OUT UINT64 *Entry,
  OUT UINTN *KernelSize,
  OUT UINT64 *BootInfoSymbol,
  OUT PAGE_DESCRIPTOR **Pages
) {
  EFI_STATUS Status;

  PRINT_INFO("Loading kernel");
  PRINT_INFO("  phys addr: 0x%p", PhysAddr);

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

  Status = KernelImageHandle->Close(KernelImageHandle);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to close kernel image handle");
    FreePool(KernelImageBuffer);
    return Status;
  }

  PRINT_INFO("  image size: %llu", KernelImageSize);

  UINT64 KernelEntry;
  UINTN MemSize;
  Status = ReadElf(KernelImageBuffer, &KernelEntry, &MemSize);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Bad kernel image format");
    FreePool(KernelImageBuffer);
    return Status;
  }

  PRINT_INFO("  kernel entry: 0x%p", KernelEntry);
  PRINT_INFO("  memory size: %llu (%llu pages)", MemSize, EFI_SIZE_TO_PAGES(MemSize));

  // load elf segments
  PAGE_DESCRIPTOR *KernelPages = NULL;
  Status = LoadElf(KernelImageBuffer, PhysAddr, &KernelPages);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to load kernel image");
    FreePool(KernelImageBuffer);
    return Status;
  }

  // load sections requested by the kernel
  Status = LoadKernelRequestedSections(KernelImageBuffer, KernelPages);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to load kernel requested sections");
    FreePool(KernelImageBuffer);
    return Status;
  }

  // locate boot_info symbol (if any)
  Elf64_Sym *BootInfoSym = LocateKernelBootInfoSymbol(KernelImageBuffer);

  *Entry = KernelEntry;
  *KernelSize = KernelImageSize;
  *BootInfoSymbol = BootInfoSym != NULL ? BootInfoSym->st_value : 0;
  *Pages = KernelPages;

  PRINT_INFO("Kernel loaded");
  FreePool(KernelImageBuffer);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LoadRawFile(
  IN CONST CHAR16 *Path,
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINT64 LoadMinimumAddress,
  OUT UINT64 *FileAddr,
  OUT UINT64 *FileSize
) {
  EFI_STATUS Status;
  EFI_FILE *File = NULL;

  PRINT_INFO("Loading file %s", Path);

  Status = OpenFile(Path, &File);
  if (EFI_ERROR(Status))
    return Status;

  EFI_FILE_INFO *FileInfo;
  Status = GetFileInfo(File, &FileInfo);
  if (EFI_ERROR(Status))
    goto LOAD_ERROR;

  // find place to load the initrd
  UINT64 PhysAddr;
  UINTN NumPages = EFI_SIZE_TO_PAGES(FileInfo->FileSize) + 1;
  FreePool(FileInfo);
  Status = LocateFreeMemoryRegion(MemoryMap, NumPages, LoadMinimumAddress, &PhysAddr);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to locate free memory region for file");
    goto LOAD_ERROR;
  }

  UINTN BufferSize;
  VOID *Buffer;
  Status = ReadFile(File, &BufferSize, &Buffer);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to read file");
    goto LOAD_ERROR;
  }

  PRINT_INFO("  addr: 0x%p", PhysAddr);
  PRINT_INFO("  size: %llu (%llu pages)", BufferSize, EFI_SIZE_TO_PAGES(BufferSize));

  // load it at the physical address
  UINTN MemorySize = EFI_PAGES_TO_SIZE(NumPages);
  CopyMem((VOID *) PhysAddr, Buffer, BufferSize);
  ZeroMem((VOID *) (PhysAddr + BufferSize), MemorySize - BufferSize);

  *FileAddr = PhysAddr;
  *FileSize = MemorySize;
  CloseFile(File);
  FreePool(Buffer);
  return EFI_SUCCESS;

LOAD_ERROR:
  CloseFile(File);
  return Status;
}

//
// MARK: Debugging
//

EFI_STATUS EFIAPI PrintElfInfo(IN VOID *Buffer) {
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
