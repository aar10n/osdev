//
// Created by Aaron Gill-Braun on 2024-01-17.
//

#include <kernel/loadelf.h>
#include <kernel/exec.h>
#include <kernel/fs.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include <elf.h>

#define ASSERT(x) kassert(x)
#define EPRINTF(fmt, ...) kprintf("loadelf: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...) kprintf("loadelf: %s: " fmt, __func__, ##__VA_ARGS__)

static inline bool is_elf_magic(Elf64_Ehdr *elf) {
  return elf->e_ident[EI_MAG0] == ELFMAG0 &&
         elf->e_ident[EI_MAG1] == ELFMAG1 &&
         elf->e_ident[EI_MAG2] == ELFMAG2 &&
         elf->e_ident[EI_MAG3] == ELFMAG3;
}

static size_t elf_get_image_filesz(Elf64_Ehdr *ehdr) {
  return ehdr->e_ehsize + (ehdr->e_phnum * ehdr->e_phentsize) + (ehdr->e_shnum * ehdr->e_shentsize);
}

//

bool elf_is_valid_file(void *file_base, size_t len) {
  if (len < sizeof(Elf64_Ehdr)) {
    return false;
  }

  Elf64_Ehdr *ehdr = file_base;
  if (!is_elf_magic(ehdr)) {
    return false;
  }

  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
      ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
      ehdr->e_ident[EI_VERSION] != EV_CURRENT ||
      ehdr->e_ident[EI_OSABI] != ELFOSABI_SYSV ||
      ehdr->e_ident[EI_ABIVERSION] != 0) {
    return false;
  }
  return true;
}

int elf_load_image(int fd, void *file_base, size_t len, uint32_t e_type, uintptr_t base, __inout struct exec_image *image) {
  Elf64_Ehdr *ehdr = file_base;
  if (len < elf_get_image_filesz(ehdr)) {
    DPRINTF("malformed elf file\n");
    return -EINVAL;
  }

  if (ehdr->e_type != e_type) {
    if (e_type == ET_EXEC) {
      DPRINTF("program is not an executable\n");
    } else if (e_type == ET_DYN) {
      DPRINTF("program is not a shared object\n");
    }
    return -ENOEXEC;
  }

  // iterate through the program headers
  int res;
  size_t loaded_size = 0;
  str_t interp = str_null;
  uint64_t min_vaddr = UINT64_MAX;
  LIST_HEAD(vm_desc_t) descs = {0};
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    switch (phdr[i].p_type) {
      case PT_LOAD:
        break; // load the segment
      case PT_PHDR:
        image->phdr = image->base + phdr[i].p_vaddr;
        continue;
      case PT_INTERP:
        ASSERT(str_isnull(interp));
        interp = str_from((const char *) (file_base + phdr[i].p_offset));
        continue;
      default:
        continue;
    }
    if (phdr[i].p_memsz == 0)
      continue;

    uint64_t vaddr = page_trunc(phdr[i].p_vaddr);
    uint64_t end_vaddr = page_align(phdr[i].p_vaddr + phdr[i].p_memsz);
    uint64_t off = page_trunc(phdr[i].p_offset);
    uint64_t end_off = page_align(phdr[i].p_offset + phdr[i].p_filesz);
    size_t filesz = end_off - off;
    size_t memsz = end_vaddr - vaddr;
    uint32_t flags = phdr[i].p_flags;
    min_vaddr = min(min_vaddr, vaddr);

    uint32_t vm_flags = VM_READ | VM_PRIVATE;
    if (phdr[i].p_flags & PF_X)
      vm_flags |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      vm_flags |= VM_WRITE;

    if (phdr[i].p_filesz == phdr[i].p_memsz) {
      // // this is a normal segment which means we can just have the file mmap'd
      // vm_file_t *seg_file = fs_get_vm_file(fd, off, filesz);
      // vm_desc_t *seg_desc = vm_desc_alloc(VM_TYPE_FILE, base + vaddr, filesz, vm_flags, "elf_seg", seg_file);
      // SLIST_ADD(&descs, seg_desc, next);
      off_t cur_off = (off_t) off;
      size_t rem_filesz = filesz;
      page_t *data_pages = NULL;
      while (rem_filesz > 0) {
        data_pages = page_list_join(moveref(data_pages), fs_getpage(fd, cur_off));
        rem_filesz -= PAGE_SIZE;
        cur_off += PAGE_SIZE;
      }

      vm_desc_t *seg_desc = vm_desc_alloc(VM_TYPE_PAGE, base + vaddr, memsz, vm_flags, "elf_seg", moveref(data_pages));
      SLIST_ADD(&descs, seg_desc, next);
    } else if (phdr[i].p_filesz < phdr[i].p_memsz) {
      // this is a data segment that contains the bss (uninitialized data) section
      off_t cur_off = (off_t) off;
      size_t rem_filesz = filesz;
      page_t *data_pages = NULL;
      while (rem_filesz > 0) {
        data_pages = page_list_join(moveref(data_pages), fs_getpage(fd, cur_off));
        rem_filesz -= PAGE_SIZE;
        cur_off += PAGE_SIZE;
      }

      if (memsz > filesz) {
        // the unititialized memory spans more than just the pages from the file
        data_pages = page_list_join(moveref(data_pages), alloc_pages(SIZE_TO_PAGES(memsz - filesz)));
      }

      // zero any extraneous bytes at the end of the file pages
      size_t bss_off = (phdr[i].p_vaddr - vaddr) + phdr[i].p_filesz;
      size_t bss_size = phdr[i].p_memsz - phdr[i].p_filesz;
      fill_unmapped_pages(data_pages, 0, bss_off, bss_size);

      // create a descriptor for mapping the pages
      vm_desc_t *seg_desc = vm_desc_alloc(VM_TYPE_PAGE, base + vaddr, memsz, vm_flags, "elf_seg", moveref(data_pages));
      SLIST_ADD(&descs, seg_desc, next);
    } else {
      unreachable;
    }
  }

  image->base = base + min_vaddr;
  image->entry = base + ehdr->e_entry;
  image->size = loaded_size;
  image->phnum = ehdr->e_phnum;
  image->descs = moveptr(LIST_FIRST(&descs));

  if (!str_isnull(interp)) {
    // handle the interpreter
    if ((res = exec_load_image(EXEC_DYN, LIBC_BASE_ADDR, cstr_from_str(interp), &image->interp)) < 0) {
      DPRINTF("failed to load interpreter '{:str}' {:err}\n", &interp, res);
      goto ret;
    }
  }

  res = 0; // success
LABEL(ret);
  vm_desc_free_all(&LIST_FIRST(&descs));
  str_free(&interp);
  return res;
}
