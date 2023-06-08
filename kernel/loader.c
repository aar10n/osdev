//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#include <loader.h>

#include <cpu/cpu.h>

#include <mm.h>
#include <elf.h>
#include <thread.h>
#include <panic.h>
#include <printf.h>
#include <string.h>

#include <elf64.h>

int elf_pt_load(Elf64_Phdr *pheader, void *buf, elf_program_t *prog) {
  uint32_t flags = PG_USER;
  if (pheader->p_flags & PF_X)
    flags |= PG_EXEC;
  if (pheader->p_flags & PF_W)
    flags |= PG_WRITE;

  size_t memsz = align_down(pheader->p_memsz, pheader->p_align) + pheader->p_align;

  page_t *pages = _alloc_pages(SIZE_TO_PAGES(memsz), flags);
  uintptr_t v_aligned = align_down(pheader->p_vaddr + prog->base, pheader->p_align);
  void *addr = (void *)(pheader->p_vaddr + prog->base);

  void *res = _vmap_pages_addr(v_aligned, pages);
  if (res == NULL) {
    panic("exec: could not load executable");
  }

  // disable write protection just long enough for us to
  // copy over and zero the allocated pages even if the
  // pages are not marked as writable
  cpu_disable_write_protection();
  memset(res, 0, memsz);
  memcpy(addr, buf + pheader->p_offset, pheader->p_filesz);
  cpu_enable_write_protection();

  if (prog->prog_pages) {
    page_t *last = prog->prog_pages;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = pages;
  } else {
    prog->prog_pages = pages;
  }
  return 0;
}

int elf_pt_dynamic(Elf64_Phdr *pheader, void *buf, elf_program_t *prog) {
  if (pheader->p_align >= PAGE_SIZE) {
    return elf_pt_load(pheader, buf, prog);
  }

  uintptr_t page_boundary = align_down(pheader->p_vaddr + prog->base, PAGE_SIZE);
  vm_mapping_t *mapping = _vmap_get_mapping(page_boundary);
  if (mapping == NULL) {
    return elf_pt_load(pheader, buf, prog);
  }

  void *addr = (void *)(pheader->p_vaddr + prog->base);
  kassert(pheader->p_vaddr + pheader->p_memsz < page_boundary + mapping->size);
  cpu_disable_write_protection();
  memcpy(addr, buf + pheader->p_offset, pheader->p_filesz);
  cpu_enable_write_protection();
  return 0;
}

int elf_pt_interp(Elf64_Phdr *pheader, void *buf, elf_program_t *prog) {
  char *interp = kmalloc(pheader->p_filesz + 1);
  memcpy(interp, buf + pheader->p_offset, pheader->p_filesz);
  interp[pheader->p_filesz] = '\0';
  prog->interp = interp;
  return 0;
}

//

int load_elf(void *buf, elf_program_t *prog) {
  Elf64_Ehdr *elf = buf;
  if (!IS_ELF(*elf)) {
    ERRNO = ENOEXEC;
    return -1;
  }

  prog->entry = elf->e_entry + prog->base;
  prog->phent = elf->e_phentsize;
  prog->phnum = elf->e_phnum;

  Elf64_Phdr *phead = buf + elf->e_phoff;
  for (uint32_t i = 0; i < elf->e_phnum; i++) {
    if (phead[i].p_type == PT_LOAD && phead[i].p_memsz > 0) {
      elf_pt_load(&phead[i], buf, prog);
    } else if (phead[i].p_type == PT_DYNAMIC) {
      elf_pt_dynamic(&phead[i], buf, prog);
    } else if (phead[i].p_type == PT_INTERP) {
      elf_pt_interp(&phead[i], buf, prog);
    } else if (phead[i].p_type == PT_PHDR) {
      prog->phdr = phead[i].p_vaddr + prog->base;
    }
  }
  return 0;
}

//

int load_elf_file(const char *path, elf_program_t *prog) {
  unimplemented("load_elf_file");
  // int fd = fs_open(path, O_RDONLY, 0);
  // if (fd < 0) {
  //   return -1;
  // }
  //
  // stat_t stat;
  // if (fs_fstat(fd, &stat) < 0) {
  //   fs_close(fd);
  //   return -1;
  // }
  //
  // page_t *program = valloc_pages(SIZE_TO_PAGES(stat.st_size), PG_WRITE);
  // ssize_t nread = fs_read(fd, (void *) PAGE_VIRT_ADDR(program), stat.st_size);
  // if (nread < 0) {
  //   vfree_pages(program);
  //   fs_close(fd);
  //   return -1;
  // }
  //
  // prog->file_pages = program;
  // if (load_elf((void *) PAGE_VIRT_ADDR(program), prog) < 0) {
  //   vfree_pages(program);
  //   fs_close(fd);
  // }
  //
  // kprintf("loaded %s at %018p [entry = %018p]\n", path, prog->base, prog->entry);
  // if (prog->interp != NULL) {
  //   elf_program_t *linker = kmalloc(sizeof(elf_program_t));
  //   memset(linker, 0, sizeof(elf_program_t));
  //
  //   linker->base = 0x7FC0000000;
  //   if (load_elf_file("/lib/ld.so", linker) < 0) {
  //     vfree_pages(program);
  //     fs_close(fd);
  //     panic("failed to load ld.so\n");
  //   }
  //
  //   prog->linker = linker;
  // }
  // return 0;
}
