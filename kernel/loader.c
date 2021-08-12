//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#include <loader.h>
#include <cpu/cpu.h>
#include <thread.h>
#include <mm.h>
#include <elf.h>
#include <elf64.h>
#include <printf.h>
#include <string.h>
#include <path.h>
#include <fs/utils.h>

// 1. look for PT_INTERP to find out the name of the dynamic linker binary
// 2. load the executable into memory
// 3. pick a base address for ld.so
// 4. load ld.so at that base address (basically the same as loading the executable, except you add the base to all addresses)
// 5. create an auxvector on the stack of the new process
// 6. make the new process start executing at the ld.so entry point (as opposed to using the executable's entry point)

static size_t get_total_memsz(Elf64_Ehdr *elf, void *buf) {
  size_t size = 0;
  Elf64_Phdr *phead = buf + elf->e_phoff;
  for (uint32_t i = 0; i < elf->e_phnum; i++) {
    if (phead[i].p_type == PT_LOAD && phead[i].p_memsz > 0) {
      size += phead[i].p_memsz;
    }
  }
  return size;
}

//

int elf_pt_load(Elf64_Phdr *pheader, void *buf, elf_program_t *prog) {
  uint16_t flags = PE_USER;
  if (pheader->p_flags & PF_X)
    flags |= PE_EXEC;
  if (pheader->p_flags & PF_W)
    flags |= PE_WRITE;

  page_t *pages = alloc_frames(SIZE_TO_PAGES(pheader->p_memsz), flags);
  void *addr = vm_map_page_vaddr(pheader->p_vaddr + prog->base, pages);

  // disable write protection just long enough for us to
  // copy over and zero the allocated pages even if the
  // pages are not marked as writable
  uint64_t cr0 = read_cr0();
  write_cr0(cr0 & ~(1 << 16)); // disable cr0.WP
  memcpy(addr, buf + pheader->p_offset, pheader->p_filesz);
  memset(offset_ptr(addr, pheader->p_filesz), 0, pheader->p_memsz - pheader->p_filesz);
  write_cr0(cr0); // re-enable cr0.WP

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
    } else if (phead[i].p_type == PT_INTERP) {
      elf_pt_interp(&(phead[i]), buf, prog);
    } else if (phead[i].p_type == PT_PHDR) {
      prog->phdr = phead[i].p_offset + prog->base;
    }
  }
  return 0;
}

//

int load_elf_file(const char *path, elf_program_t *prog) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    return -1;
  }

  kstat_t stat;
  if (fs_fstat(fd, &stat) < 0) {
    fs_close(fd);
    return -1;
  }

  page_t *program = alloc_pages(SIZE_TO_PAGES(stat.size), PE_WRITE);
  ssize_t nread = fs_read(fd, (void *) program->addr, stat.size);
  if (nread < 0) {
    free_pages(program);
    fs_close(fd);
    return -1;
  }

  prog->file_pages = program;
  if (load_elf((void *) program->addr, prog) < 0) {
    free_pages(program);
    fs_close(fd);
  }

  if (prog->interp != NULL) {
    elf_program_t *linker = kmalloc(sizeof(elf_program_t));
    memset(linker, 0, sizeof(elf_program_t));

    linker->base = 0x7FC0000000;
    if (load_elf_file("/usr/lib/ld.so", linker) < 0) {
      free_pages(program);
      fs_close(fd);
    }

    prog->linker = linker;
  }
  return 0;
}
