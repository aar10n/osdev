//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#include <kernel/loader.h>

#include <kernel/cpu/cpu.h>

#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/thread.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <elf.h>
#include <elf64.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("loader: " fmt, ##__VA_ARGS__)


static inline uint64_t elf_align_up(uint64_t value, uint64_t align) {
  if (align == 0 || align == 1) {
    return value;
  }
  return align(value, align);
}

static inline uint64_t elf_align_down(uint64_t value, uint64_t align) {
  if (align == 0 || align == 1) {
    return value;
  }
  return align_down(value, align);
}

static bool elf_is_valid_executable(void *buf, size_t len) {
  if (len < sizeof(Elf64_Ehdr)) {
    return false;
  }

  Elf64_Ehdr *elf = buf;
  if (!IS_ELF(*elf)) {
    DPRINTF("invalid magic number\n");
    return false;
  }

  if (elf->e_ident[EI_CLASS] != ELFCLASS64 ||
    elf->e_ident[EI_DATA] != ELFDATA2LSB ||
      elf->e_ident[EI_VERSION] != EV_CURRENT ||
      elf->e_ident[EI_OSABI] != ELFOSABI_SYSV ||
      elf->e_ident[EI_ABIVERSION] != 0) {
    DPRINTF("invalid header\n");
    return false;
  }

  if (elf->e_type != ET_EXEC) {
    DPRINTF("program is not an executable\n");
    return false;
  }
  return true;
}

//

int elf_pt_load(elf_program_t *prog, Elf64_Phdr *pheader, void *buf, size_t len) {
  if (pheader->p_memsz == 0) {
    return 0;
  }

  uint64_t offset = pheader->p_offset;
  uint64_t filesz = pheader->p_filesz;
  uint64_t memsz = elf_align_up(pheader->p_memsz, pheader->p_align);
  uint64_t vaddr = prog->base + pheader->p_vaddr;

  uint32_t pg_flags = PG_USER;
  if (pheader->p_flags & PF_X)
    pg_flags |= PG_EXEC;
  if (pheader->p_flags & PF_W)
    pg_flags |= PG_WRITE;

  uintptr_t boundary = prog->base + elf_align_down(pheader->p_vaddr, pheader->p_align);
  vm_mapping_t *vm = vm_get_mapping(boundary);
  if (vm == NULL) {
    DPRINTF("allocating memory for loadable segment\n");

    // allocate memory for the segment
    page_t *pages = alloc_pages(SIZE_TO_PAGES(memsz));
    if (pages == NULL) {
      DPRINTF("failed to allocate pages for loadable segment\n");
      return -ENOMEM;
    }

    vm = vm_alloc_map_pages(pages, boundary, memsz, VM_FIXED|VM_USER, pg_flags, "elf.segment");
    if (vm == NULL) {
      DPRINTF("failed to allocate or map loadable segment\n");
      free_pages(pages);
      return -ENOMEM;
    }

    // zero out the newly allocated pages
    cpu_disable_write_protection();
    memset((void *) boundary, 0, memsz);
    cpu_enable_write_protection();
  }

  kprintf("elf load [virt=%p, filesz=%zu, memsz=%zu, offset=%zu]\n",
          vaddr, filesz, memsz, offset);

  cpu_disable_write_protection();
  memcpy((void *) vaddr, buf + offset, filesz);
  cpu_enable_write_protection();
  return 0;
}

int elf_pt_interp(elf_program_t *prog, Elf64_Phdr *pheader, void *buf, size_t len) {
  char *interp = kmalloc(pheader->p_filesz + 1);
  memcpy(interp, buf + pheader->p_offset, pheader->p_filesz);
  interp[pheader->p_filesz] = '\0';
  prog->interp = interp;
  return 0;
}

//

int elf_load(elf_program_t *prog, void *buf, size_t len) {
  Elf64_Ehdr *elf = buf;
  if (!(elf_is_valid_executable(buf, len))) {
    DPRINTF("invalid elf executuable\n");
    return -ENOEXEC;
  }

  prog->entry = prog->base + elf->e_entry;
  prog->phent = elf->e_phentsize;
  prog->phnum = elf->e_phnum;

  uint64_t min_vaddr = UINT64_MAX;
  uint64_t max_vaddr = 0;
  Elf64_Phdr *phead = buf + elf->e_phoff;
  for (uint32_t i = 0; i < elf->e_phnum; i++) {
    if (phead[i].p_type != PT_LOAD || phead[i].p_memsz == 0)
      continue;

    uint64_t vaddr = phead[i].p_vaddr;
    uint64_t memsz = phead[i].p_memsz;
    if (vaddr < min_vaddr)
      min_vaddr = vaddr;
    if (vaddr + memsz > max_vaddr)
      max_vaddr = vaddr + memsz;
  }

  size_t image_size = max_vaddr - min_vaddr;
  if (image_size == 0) {
    return -ENOEXEC;
  }

  // page_t *pages = alloc_pages();
  // vm_mapping_t *vm = vm_all

  kprintf("elf load [min_vaddr=%p, max_vaddr=%p]\n", min_vaddr, max_vaddr);

  ASSERT(false);

  // for (uint32_t i = 0; i < elf->e_phnum; i++) {
  //   if (phead[i].p_type == PT_LOAD || phead[i].p_type == PT_DYNAMIC) {
  //     elf_pt_load(prog, &phead[i], buf, 0);
  //   } else if (phead[i].p_type == PT_INTERP) {
  //     elf_pt_interp(prog, &phead[i], buf, 0);
  //   } else if (phead[i].p_type == PT_PHDR) {
  //     prog->phdr = phead[i].p_vaddr + prog->base;
  //   }
  // }
  return 0;
}

//

int elf_load_file(const char *path, elf_program_t *prog) {
  int res;
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    return fd;
  }

  stat_t stat;
  if ((res = fs_fstat(fd, &stat)) < 0) {
    fs_close(fd);
    return res;
  }

  void *program = vmalloc(stat.st_size, PG_USER|PG_WRITE);
  ssize_t nread = fs_read(fd, program, stat.st_size);
  if (nread < 0) {
    vfree(program);
    fs_close(fd);
    return (int) nread;
  }

  if ((res = elf_load(prog, program, 0)) < 0) {
    vfree(program);
    fs_close(fd);
    return res;
  }

  kprintf("loaded %s at %018p [entry = %018p]\n", path, prog->base, prog->entry);
  if (prog->interp != NULL) {
    kprintf("interp = %s\n", prog->interp);
    // elf_program_t *linker = kmalloc(sizeof(elf_program_t));
    // memset(linker, 0, sizeof(elf_program_t));
    //
    // linker->base = 0x7FC0000000;
    // if (elf_load_file("/lib/ld.so", linker) < 0) {
    //   vfree(program);
    //   fs_close(fd);
    //   panic("failed to load ld.so\n");
    // }
    //
    // prog->linker = linker;
  }
  kprintf("done\n");
  return 0;
}
