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
#define DPRINTF(fmt, ...) kprintf("loadelf: " fmt, ##__VA_ARGS__)

static inline uint64_t elf_align_up(uint64_t value, uint64_t align) {
  if (align == 0 || align == 1 || is_aligned(value, align)) {
    return value;
  }
  return align(value, align);
}

static inline uint64_t elf_align_down(uint64_t value, uint64_t align) {
  if (align == 0 || align == 1 || is_aligned(value, align)) {
    return value;
  }
  return align_down(value, align);
}

static inline bool is_elf_magic(Elf64_Ehdr *elf) {
  return elf->e_ident[EI_MAG0] == ELFMAG0 &&
         elf->e_ident[EI_MAG1] == ELFMAG1 &&
         elf->e_ident[EI_MAG2] == ELFMAG2 &&
         elf->e_ident[EI_MAG3] == ELFMAG3;
}

static size_t elf_get_image_filesz(Elf64_Ehdr *ehdr) {
  return ehdr->e_ehsize + (ehdr->e_phnum * ehdr->e_phentsize) + (ehdr->e_shnum * ehdr->e_shentsize);
}

static size_t elf_get_loaded_memsz(Elf64_Ehdr *ehdr, uint64_t *out_min_vaddr) {
  // determine the size of the loaded image
  uint64_t min_vaddr = UINT64_MAX;
  uint64_t max_vaddr = 0;
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_NULL || phdr[i].p_memsz == 0)
      continue;

    uint64_t vaddr = phdr[i].p_vaddr;
    uint64_t memsz = phdr[i].p_memsz;
    if (vaddr < min_vaddr)
      min_vaddr = vaddr;
    if (vaddr + memsz > max_vaddr)
      max_vaddr = vaddr + memsz;
  }

  min_vaddr = elf_align_down(min_vaddr, PAGE_SIZE);
  max_vaddr = elf_align_up(max_vaddr, PAGE_SIZE);
  if (out_min_vaddr != NULL) {
    *out_min_vaddr = min_vaddr;
  }
  return max_vaddr - min_vaddr;
}

//

bool elf_is_valid_file(void *filebuf, size_t len) {
  if (len < sizeof(Elf64_Ehdr)) {
    return false;
  }

  Elf64_Ehdr *ehdr = filebuf;
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

int elf_load_image(void *filebuf, size_t len, uint32_t e_type, struct exec_image *image) {
  Elf64_Ehdr *ehdr = filebuf;
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

  uint64_t min_vaddr = 0;
  size_t loaded_size = elf_get_loaded_memsz(ehdr, &min_vaddr);
  if (loaded_size == 0) {
    DPRINTF("no loadable segments\n");
    return -EINVAL;
  }

  min_vaddr += image->base;
  image->entry = ehdr->e_entry;
  image->size = loaded_size;

  // page_t *pages = alloc_pages(SIZE_TO_PAGES(loaded_size));
  // if (pages == NULL) {
  //   DPRINTF("not enough memory\n");
  //   return -ENOMEM;
  // }



  // prog->phentsize = ehdr->e_phentsize;
  // prog->phnum = ehdr->e_phnum;
  // prog->interp_path = str_null;

  // allocate a mapping for the loaded image
  // uintptr_t base = vm_map_anon(0, min_vaddr, loaded_size, vm_flags, prog->path);
  // if (base == 0) {
  //   return -ENOMEM;
  // }
  // uintptr_t end = offset_addr(base, loaded_size);


  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    switch (phdr[i].p_type) {
      case PT_LOAD:
        if (phdr[i].p_memsz == 0)
          continue;
        else
          break;
      case PT_PHDR:
        // prog->phdr = imagep->base + phdr[i].p_vaddr;
        continue;
      case PT_INTERP:
        image->interp_path = str_from(offset_ptr(ehdr, phdr[i].p_offset));
        continue;
      default:
        continue;
    }

    // load the segment into memory
    // uint64_t vaddr = imagep->base + ;
    uint64_t loadbase = elf_align_down(phdr[i].p_vaddr, phdr[i].p_align);
    uint64_t offset = phdr[i].p_offset;
    uint64_t filesz = phdr[i].p_filesz;
    uint64_t memsz = elf_align_up(phdr[i].p_memsz, phdr[i].p_align);

    uint32_t prot = VM_READ;
    if (phdr[i].p_flags & PF_X)
      prot |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      prot |= VM_WRITE;



    // // copy the data
    // memcpy((void *) vaddr, buf + offset, filesz);
    // // zero out any extra memory following the loaded data
    // memset((void *) (vaddr + filesz), 0, memsz - filesz);
    // if ((vaddr - loadbase) > 0) {
    //   memset((void *) loadbase, 0, vaddr - loadbase);
    // }

    // // update the page protection
    // if (vm_protect(loadbase, memsz, prot) < 0) {
    //   DPRINTF("elf_load_image: failed to update memory protection\n");
    //   vm_free(base, loaded_size);
    //   return -EINVAL;
    // }
  }


  return 0;
}

// int elf_load_image(elf_program_t *prog, void *buf, size_t len, uint32_t e_type) {
//   Elf64_Ehdr *ehdr = buf;
//   if (len < sizeof(Elf64_Ehdr)) {
//     DPRINTF("buffer too small\n");
//     return -ENOEXEC;
//   } else if (!(elf_is_valid(buf, len, e_type))) {
//     DPRINTF("invalid elf file\n");
//     return -ENOEXEC;
//   }
//
//   uint64_t min_vaddr = 0;
//   size_t loaded_size = elf_get_loaded_memsz(ehdr, &min_vaddr);
//   if (loaded_size == 0) {
//     DPRINTF("no loadable segments\n");
//     return -ENOEXEC;
//   } else if (len < elf_get_image_filesz(ehdr)) {
//     DPRINTF("invalid header or buffer too small\n");
//     return -ENOEXEC;
//   }
//
//   min_vaddr += prog->base;
//   prog->entry = prog->base + ehdr->e_entry;
//   prog->phentsize = ehdr->e_phentsize;
//   prog->phnum = ehdr->e_phnum;
//   prog->interp_path = str_null;
//
//   uint32_t vm_flags = VM_WRITE | (is_user_ptr(min_vaddr) ? VM_USER : 0);
//   if (ehdr->e_type == ET_EXEC)
//     vm_flags |= VM_FIXED;
//
//   // allocate a mapping for the loaded image
//   uintptr_t base = vm_map_anon(0, min_vaddr, loaded_size, vm_flags, prog->path);
//   if (base == 0) {
//     return -ENOMEM;
//   }
//   prog->end = offset_addr(base, loaded_size);
//
//   Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
//   for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
//     switch (phdr[i].p_type) {
//       case PT_LOAD:
//         if (phdr[i].p_memsz == 0)
//           continue;
//         else
//           break;
//       case PT_PHDR:
//         prog->phdr = prog->base + phdr[i].p_vaddr;
//         continue;
//       case PT_INTERP:
//         prog->interp_path = str_from(offset_ptr(ehdr, phdr[i].p_offset));
//         continue;
//       default:
//         continue;
//     }
//
//     // load the segment into memory
//     uint64_t vaddr = prog->base + phdr[i].p_vaddr;
//     uint64_t loadbase = elf_align_down(vaddr, phdr[i].p_align);
//     uint64_t offset = phdr[i].p_offset;
//     uint64_t filesz = phdr[i].p_filesz;
//     uint64_t memsz = elf_align_up(phdr[i].p_memsz, phdr[i].p_align);
//
//     uint32_t prot = VM_READ;
//     if (phdr[i].p_flags & PF_X)
//       prot |= VM_EXEC;
//     if (phdr[i].p_flags & PF_W)
//       prot |= VM_WRITE;
//
//     // copy the data
//     memcpy((void *) vaddr, buf + offset, filesz);
//     // zero out any extra memory following the loaded data
//     memset((void *) (vaddr + filesz), 0, memsz - filesz);
//     if ((vaddr - loadbase) > 0) {
//       memset((void *) loadbase, 0, vaddr - loadbase);
//     }
//
//     // update the page protection
//     if (vm_protect(loadbase, memsz, prot) < 0) {
//       DPRINTF("elf_load_image: failed to update memory protection\n");
//       vm_free(base, loaded_size);
//       return -EINVAL;
//     }
//   }
//   return 0;
// }

// int elf_load_file(elf_program_t *prog, const char *path, uint32_t e_type) {
//   DPRINTF("loading elf file '%s'\n", path);
//   int res;
//   int fd = fs_open(path, O_RDONLY, 0);
//   if (fd < 0) {
//     DPRINTF("failed to open file '%s' {:err}\n", path, fd);
//     return fd;
//   }
//
//   struct stat stat;
//   if ((res = fs_fstat(fd, &stat)) < 0) {
//     fs_close(fd);
//     return res;
//   }
//
//   // TODO: we should load the shared libc into kernel space and then
//   //       mmap it into user space
//   void *buffer = vmalloc(stat.st_size, VM_USER);
//   // read the file into memory
//   ssize_t nread = fs_read(fd, buffer, stat.st_size);
//   if (nread < 0) {
//     res = (int) nread;
//     goto ret;
//   }
//
//   // load the elf image
//   prog->path = strdup(path);
//   if ((res = elf_load_image(prog, buffer, stat.st_size, e_type)) < 0) {
//     goto ret;
//   }
//
//   prog->interp = NULL;
//   if (!str_isnull(prog->interp_path)) {
//     // load the interpreter
//     ASSERT(e_type == ET_EXEC);
//     DPRINTF("interp = {:str}\n", &prog->interp_path);
//     prog->interp = kmallocz(sizeof(elf_program_t));
//     prog->interp->base = LIBC_BASE_ADDR;
//     if ((res = elf_load_file(prog->interp, "/initrd/lib/ld-musl-x86_64.so.1", ET_DYN)) < 0) {
//       DPRINTF("failed to load interpreter '{:str}' {:err}\n", &prog->interp_path, res);
//       kfree(prog->interp);
//       prog->interp = NULL;
//       goto ret;
//     }
//   }
//
//   DPRINTF("loaded elf file %s [base = %p, entry = %p]\n", path, prog->base, prog->entry);
//   res = 0; // success
// LABEL(ret);
//   if (res < 0) {
//     vfree(buffer);
//     kfree(prog->path);
//   }
//   if (fd >= 0)
//     fs_close(fd);
//   return res;
// }
