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

static bool elf_is_valid(void *buf, size_t len) {
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
  // if (elf->e_type != ET_EXEC) {
  //   DPRINTF("program is not an executable\n");
  //   return false;
  // }
  return true;
}

static size_t elf_get_loaded_size(Elf64_Ehdr *ehdr, uint64_t *out_min_vaddr) {
  // determine the size of the loaded image
  uint64_t min_vaddr = UINT64_MAX;
  uint64_t max_vaddr = 0;
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0)
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

int elf_load(elf_program_t *prog, void *buf, size_t len) {
  Elf64_Ehdr *ehdr = buf;
  if (!(elf_is_valid(buf, len))) {
    DPRINTF("invalid elf file\n");
    return -ENOEXEC;
  }

  uint64_t min_vaddr = 0;
  size_t image_size = elf_get_loaded_size(ehdr, &min_vaddr);
  if (image_size == 0) {
    DPRINTF("no loadable segments\n");
    return -ENOEXEC;
  }

  min_vaddr += prog->base;
  prog->entry = prog->base + ehdr->e_entry;
  prog->phent = ehdr->e_phentsize;
  prog->phnum = ehdr->e_phnum;

  uint32_t vm_flags = VM_WRITE | (is_user_ptr(min_vaddr) ? VM_USER : 0);
  if (ehdr->e_type == ET_EXEC)
    vm_flags |= VM_FIXED;

  // allocate and map memory for the image
  page_t *pages = alloc_pages(SIZE_TO_PAGES(image_size));
  if (pages == NULL)
    return -ENOMEM;

  kprintf("ehdr = %018p [phoff = %d]\n", ehdr, ehdr->e_phoff);
  vm_mapping_t *vm = vmap_pages(pages, min_vaddr, image_size, vm_flags, prog->path);
  if (vm == NULL) {
    free_pages(pages);
    return -ENOMEM;
  }

  // load each loadable segment
  kprintf("ehdr = %018p [phoff = %d]\n", ehdr, ehdr->e_phoff);
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_INTERP) {
      char *interp = kmalloc(phdr[i].p_filesz + 1);
      memcpy(interp, buf + phdr[i].p_offset, phdr[i].p_filesz);
      interp[phdr[i].p_filesz] = '\0';
      prog->interp = interp;
    }
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) {
      continue;
    }

    uint64_t offset = phdr[i].p_offset;
    uint64_t filesz = phdr[i].p_filesz;
    uint64_t memsz = elf_align_up(phdr[i].p_memsz, phdr[i].p_align);
    uint64_t vaddr = prog->base + phdr[i].p_vaddr;

    uint32_t prot_flags = VM_READ;
    if (phdr[i].p_flags & PF_X)
      prot_flags |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      prot_flags |= VM_WRITE;

    kprintf("elf load [virt=%p, filesz=%zu, memsz=%zu, offset=%zu, prot=%03b]\n",
            vaddr, filesz, memsz, offset, prot_flags);

    vm = vm_get_mapping(vaddr);
    ASSERT(vm != NULL);

    // copy the segment into memory and update the mapping permissions
    memcpy((void *) vaddr, buf + offset, filesz);

    size_t vmoff = elf_align_down(vaddr - vm->address, phdr[i].p_align);
    if (is_aligned(memsz, PAGE_SIZE)) {
      memset((void *) (vaddr + filesz), 0, memsz - filesz);
      if (vm_update(vm, vmoff, memsz, prot_flags) < 0) {
        DPRINTF("failed to update vm mapping\n");
        vm_mapping_t *first = vm_get_mapping(min_vaddr);
        vmap_free(first);
        return -EINVAL;
      }
    }
  }

  return 0;
}

//

int elf_load_file(const char *path, elf_program_t *prog) {
  int res;
  DPRINTF("loading elf file '%s'\n", path);
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    DPRINTF("failed to open file '%s' {:err}\n", path, fd);
    return fd;
  }

  stat_t stat;
  if ((res = fs_fstat(fd, &stat)) < 0) {
    fs_close(fd);
    return res;
  }

  // TODO: replace with mmap once implemented
  void *buffer = vmalloc(stat.st_size, 0);
  ssize_t nread = fs_read(fd, buffer, stat.st_size);
  if (nread < 0) {
    vfree(buffer);
    fs_close(fd);
    return (int) nread;
  }

  prog->path = strdup(path);
  if ((res = elf_load(prog, buffer, stat.st_size)) < 0) {
    DPRINTF("failed to load elf file\n");
    vfree(buffer);
    fs_close(fd);
    return res;
  }

  DPRINTF("interp = %s\n", prog->interp);
  DPRINTF("loaded %s at %018p [entry = %018p]\n", path, prog->base, prog->entry);
  if (prog->interp != NULL) {
    elf_program_t *linker = kmalloc(sizeof(elf_program_t));
    memset(linker, 0, sizeof(elf_program_t));
    linker->base = 0x7FC0000000;
    if (elf_load_file("/initrd/lib/ld-musl-x86_64.so.1", linker) < 0) {
      vfree(buffer);
      fs_close(fd);
      panic("failed to load /lib/ld-musl-x86_64.so.1");
    }

    prog->linker = linker;
  }
  vfree(buffer);

  panic("done loading elf file");
  return 0;
}
