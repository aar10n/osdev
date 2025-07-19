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

static inline bool elf_is_executable(Elf64_Ehdr *ehdr) {
  return ehdr->e_type == ET_EXEC || (ehdr->e_type == ET_DYN && ehdr->e_entry != 0);
}

static inline bool elf_is_shared_object(Elf64_Ehdr *ehdr) {
  return ehdr->e_type == ET_DYN;
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

bool elf_needs_base(void *file_base, size_t len) {
  if (len < sizeof(Elf64_Ehdr)) {
    return false;
  }

  Elf64_Ehdr *ehdr = file_base;
  if (!is_elf_magic(ehdr)) {
    return false;
  }

  // the elf file needs a base address if it is a dynamic executable (PIE)
  return ehdr->e_type == ET_DYN && ehdr->e_entry != 0;
}

int elf_load_image(enum exec_type type, int fd, void *file_base, size_t len, uintptr_t base, __inout struct exec_image *image) {
  Elf64_Ehdr *ehdr = file_base;
  if (len < elf_get_image_filesz(ehdr)) {
    DPRINTF("malformed elf file\n");
    return -EINVAL;
  }
  
  // bounds check program headers
  if (ehdr->e_phoff > len || 
      ehdr->e_phnum > (len - ehdr->e_phoff) / sizeof(Elf64_Phdr) ||
      ehdr->e_phoff + (ehdr->e_phnum * sizeof(Elf64_Phdr)) > len) {
    DPRINTF("program headers extend beyond file bounds\n");
    return -EINVAL;
  } else if (ehdr->e_type == ET_DYN && base == 0) {
    // dynamic executables must have a base address specified
    DPRINTF("dynamic executable requires a base address\n");
    return -EINVAL;
  }

  if (type == EXEC_BIN && !elf_is_executable(ehdr)) {
    DPRINTF("program is not an executable\n");
    return -ENOEXEC;
  } else if (type == EXEC_DYN && !elf_is_shared_object(ehdr)) {
    DPRINTF("program is not a shared object\n");
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
        image->phdr = base + phdr[i].p_vaddr;
        continue;
      case PT_INTERP:
        ASSERT(str_isnull(interp));
        // bounds check interpreter string
        if (phdr[i].p_offset >= len || phdr[i].p_offset + phdr[i].p_filesz > len) {
          DPRINTF("interpreter string extends beyond file bounds\n");
          res = -EINVAL;
          goto ret;
        }
        interp = str_from((const char *)(file_base + phdr[i].p_offset));
        continue;
      default:
        continue;
    }
    if (phdr[i].p_memsz == 0)
      continue;

    // bounds check loadable segment
    if (phdr[i].p_offset >= len || 
        phdr[i].p_filesz > len - phdr[i].p_offset ||
        phdr[i].p_offset + phdr[i].p_filesz > len) {
      DPRINTF("loadable segment extends beyond file bounds\n");
      res = -EINVAL;
      goto ret;
    }

    uint64_t vaddr = page_trunc(phdr[i].p_vaddr);
    uint64_t end_vaddr = page_align(phdr[i].p_vaddr + phdr[i].p_memsz);
    uint64_t off = page_trunc(phdr[i].p_offset);
    uint64_t end_off = page_align(phdr[i].p_offset + phdr[i].p_filesz);
    size_t filesz = end_off - off;
    size_t memsz = end_vaddr - vaddr;
    uint32_t flags = phdr[i].p_flags;
    min_vaddr = min(min_vaddr, vaddr);

    uint32_t vm_flags = VM_READ | VM_USER | VM_PRIVATE | VM_FIXED;
    if (phdr[i].p_flags & PF_X)
      vm_flags |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      vm_flags |= VM_WRITE;

    if (phdr[i].p_filesz == phdr[i].p_memsz) {
      // this is a data segment that is fully initialized
      off_t cur_off = (off_t) off;
      size_t rem_filesz = filesz;
      page_t *data_pages = NULL;
      while (rem_filesz > 0) {
        page_t *page = fs_getpage_cow(fd, cur_off);
        if (!page) {
          pg_putref(&data_pages);
          res = -EIO;
          goto ret;
        }

        data_pages = raw_page_list_join(moveref(data_pages), moveref(page));
        rem_filesz -= PAGE_SIZE;
        cur_off += PAGE_SIZE;
      }

      vm_desc_t *seg_desc = vm_desc_alloc(VM_TYPE_PAGE, base + vaddr, memsz, vm_flags, "elf_seg", moveref(data_pages));
      SLIST_ADD(&descs, seg_desc, next);
      loaded_size += memsz;
    } else if (phdr[i].p_filesz < phdr[i].p_memsz) {
      // this is a data segment that contains the bss (uninitialized data) section
      uint64_t file_end_vaddr = phdr[i].p_vaddr + phdr[i].p_filesz;

      // if the last file mapped page contains part of the bss section,
      // we need to replicate a "cow" and copy the file data into a new
      // page which we will then zero out
      bool last_page_has_bss = !is_aligned(file_end_vaddr, PAGE_SIZE);

      off_t cur_off = (off_t) off;
      size_t rem_filesz = filesz;
      page_t *data_pages = NULL;
      while (rem_filesz > 0) {
        page_t *page = fs_getpage_cow(fd, cur_off);
        if (!page) {
          pg_putref(&data_pages);
          res = -EIO;
          goto ret;
        }

        if (last_page_has_bss && rem_filesz <= PAGE_SIZE) {
          // allocate a new page
          page_t *last_page = alloc_pages(1);
          if (!last_page) {
            pg_putref(&page);
            pg_putref(&data_pages);
            res = -ENOMEM;
            goto ret;
          }

          // temporarily map the current page into the kernel
          uintptr_t tmp_vaddr = vmap_pages(moveref(page), 0, PAGE_SIZE, VM_RDWR, "bss_page");
          if (tmp_vaddr == 0) {
            pg_putref(&data_pages);
            pg_putref(&last_page);
            res = -ENOMEM;
            goto ret;
          }

          // copy the data from the mapped page to the new page
          kio_t kio = kio_new_readable((void *)tmp_vaddr, PAGE_SIZE);
          rw_unmapped_pages(last_page, 0, &kio);

          // zero out the BSS portion within this page
          size_t page_file_end = file_end_vaddr & (PAGE_SIZE - 1);
          size_t page_bss_size = PAGE_SIZE - page_file_end;
          fill_unmapped_pages(last_page, 0, page_file_end, page_bss_size);

          // unmap the temporary page
          vmap_free(tmp_vaddr, PAGE_SIZE);

          page = moveref(last_page);
        }

        data_pages = raw_page_list_join(moveref(data_pages), moveref(page));
        rem_filesz -= PAGE_SIZE;
        cur_off += PAGE_SIZE;
      }

      if (memsz > filesz) {
        // the unititialized memory spans more than just the pages from the file
        // allocate extra pages needed to fill the bss section. but first calculate
        // how much of that spills past the last file page to determine how many we
        // need to allocate.
        size_t bss_pages = SIZE_TO_PAGES(memsz) - SIZE_TO_PAGES(page_align(file_end_vaddr) - vaddr);
        if (bss_pages > 0) {
          page_t *extra_pages = alloc_pages(SIZE_TO_PAGES(memsz - filesz));
          if (!extra_pages) {
            pg_putref(&data_pages);
            res = -ENOMEM;
            goto ret;
          }

          // we need to zero out the extra pages
          fill_unmapped_pages(extra_pages, 0, 0, PAGES_TO_SIZE(bss_pages));

          data_pages = raw_page_list_join(moveref(data_pages), moveref(extra_pages));
        }
      }

      // create a descriptor for mapping the pages
      vm_desc_t *seg_desc = vm_desc_alloc(VM_TYPE_PAGE, base + vaddr, memsz, vm_flags, "elf_seg", moveref(data_pages));
      SLIST_ADD(&descs, seg_desc, next);
      loaded_size += memsz;
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
      vm_desc_free_all(&image->descs); // free the descriptors we already moved
      goto ret;
    }
  }

  res = 0; // success
LABEL(ret);
  vm_desc_free_all(&LIST_FIRST(&descs));
  str_free(&interp);
  return res;
}
