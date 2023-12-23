//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#include <kernel/loader.h>

#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/thread.h>
#include <kernel/process.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <elf.h>

typedef struct elf_program {
  uintptr_t base;
  uintptr_t entry;
  uintptr_t end;

  uintptr_t phdr;
  size_t phnum;
  size_t phentsize;

  char *path;
  str_t interp_path;
  struct elf_program *interp;
} elf_program_t;

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("loader: " fmt, ##__VA_ARGS__)

#define AUX(t, v) ((auxv_t){ .type = (t), .value = (v) })
#define PUSH_STACK(sp, val) ((sp) -= sizeof(val), *(typeof(val) *)(sp) = (val))

#define USER_STACK_SIZE  0x20000  // 128 KiB

//
// MARK: ELF Loading
//

static inline size_t ptr_list_len(const uintptr_t *list) {
  if (list == NULL)
    return 0;

  size_t count = 0;
  while (*list) {
    list++;
    count++;
  }
  return count;
}

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

static bool elf_is_valid(void *buf, size_t len, uint32_t e_type) {
  if (len < sizeof(Elf64_Ehdr)) {
    return false;
  }

  Elf64_Ehdr *elf = buf;
  if (!is_elf_magic(elf)) {
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
  if (elf->e_type != e_type) {
    if (e_type == ET_EXEC)
      DPRINTF("program is not an executable\n");
    else if (e_type == ET_DYN)
      DPRINTF("program is not a shared object\n");
    return false;
  }
  return true;
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

int elf_load_image(elf_program_t *prog, void *buf, size_t len, uint32_t e_type) {
  Elf64_Ehdr *ehdr = buf;
  if (len < sizeof(Elf64_Ehdr)) {
    DPRINTF("buffer too small\n");
    return -ENOEXEC;
  } else if (!(elf_is_valid(buf, len, e_type))) {
    DPRINTF("invalid elf file\n");
    return -ENOEXEC;
  }

  uint64_t min_vaddr = 0;
  size_t loaded_size = elf_get_loaded_memsz(ehdr, &min_vaddr);
  if (loaded_size == 0) {
    DPRINTF("no loadable segments\n");
    return -ENOEXEC;
  } else if (len < elf_get_image_filesz(ehdr)) {
    DPRINTF("invalid header or buffer too small\n");
    return -ENOEXEC;
  }

  min_vaddr += prog->base;
  prog->entry = prog->base + ehdr->e_entry;
  prog->phentsize = ehdr->e_phentsize;
  prog->phnum = ehdr->e_phnum;
  prog->interp_path = str_null;

  uint32_t vm_flags = VM_WRITE | (is_user_ptr(min_vaddr) ? VM_USER : 0);
  if (ehdr->e_type == ET_EXEC)
    vm_flags |= VM_FIXED;

  // allocate a mapping for the loaded image
  uintptr_t base = vm_map_anon(0, min_vaddr, loaded_size, vm_flags, prog->path);
  if (base == 0) {
    return -ENOMEM;
  }
  prog->end = offset_addr(base, loaded_size);

  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    switch (phdr[i].p_type) {
      case PT_LOAD:
        if (phdr[i].p_memsz == 0)
          continue;
        else
          break;
      case PT_PHDR:
        prog->phdr = prog->base + phdr[i].p_vaddr;
        continue;
      case PT_INTERP:
        prog->interp_path = str_make(offset_ptr(ehdr, phdr[i].p_offset));
        continue;
      default:
        continue;
    }

    // load the segment into memory
    uint64_t vaddr = prog->base + phdr[i].p_vaddr;
    uint64_t loadbase = elf_align_down(vaddr, phdr[i].p_align);
    uint64_t offset = phdr[i].p_offset;
    uint64_t filesz = phdr[i].p_filesz;
    uint64_t memsz = elf_align_up(phdr[i].p_memsz, phdr[i].p_align);

    uint32_t prot = VM_READ;
    if (phdr[i].p_flags & PF_X)
      prot |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      prot |= VM_WRITE;

    // copy the data
    memcpy((void *) vaddr, buf + offset, filesz);
    // zero out any extra memory following the loaded data
    memset((void *) (vaddr + filesz), 0, memsz - filesz);
    if ((vaddr - loadbase) > 0) {
      memset((void *) loadbase, 0, vaddr - loadbase);
    }

    // update the page protection
    if (vm_protect(loadbase, memsz, prot) < 0) {
      DPRINTF("elf_load_image: failed to update memory protection\n");
      vm_free(base, loaded_size);
      return -EINVAL;
    }
  }
  return 0;
}

int elf_load_file(elf_program_t *prog, const char *path, uint32_t e_type) {
  DPRINTF("loading elf file '%s'\n", path);
  int res;
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    DPRINTF("failed to open file '%s' {:err}\n", path, fd);
    return fd;
  }

  struct stat stat;
  if ((res = fs_fstat(fd, &stat)) < 0) {
    fs_close(fd);
    return res;
  }

  // TODO: we should load the shared libc into kernel space and then
  //       mmap it into user space
  void *buffer = vmalloc(stat.st_size, VM_USER);
  // read the file into memory
  ssize_t nread = fs_read(fd, buffer, stat.st_size);
  if (nread < 0) {
    res = (int) nread;
    goto ret;
  }

  // load the elf image
  prog->path = strdup(path);
  if ((res = elf_load_image(prog, buffer, stat.st_size, e_type)) < 0) {
    goto ret;
  }

  prog->interp = NULL;
  if (!str_isnull(prog->interp_path)) {
    // load the interpreter
    ASSERT(e_type == ET_EXEC);
    DPRINTF("interp = {:str}\n", &prog->interp_path);
    prog->interp = kmallocz(sizeof(elf_program_t));
    prog->interp->base = LIBC_BASE_ADDR;
    if ((res = elf_load_file(prog->interp, "/initrd/lib/ld-musl-x86_64.so.1", ET_DYN)) < 0) {
      DPRINTF("failed to load interpreter '{:str}' {:err}\n", &prog->interp_path, res);
      kfree(prog->interp);
      prog->interp = NULL;
      goto ret;
    }
  }

  DPRINTF("loaded elf file %s [base = %p, entry = %p]\n", path, prog->base, prog->entry);
  res = 0; // success
LABEL(ret);
  if (res < 0) {
    vfree(buffer);
    kfree(prog->path);
  }
  if (fd >= 0)
    fs_close(fd);
  return res;
}

//
// MARK: Public API
//

int load_executable(const char *path, char *const argp[], char *const envp[], program_t *program) {
  size_t argc = ptr_list_len((void *) argp);
  size_t envc = ptr_list_len((void *) envp);
  if (argc > MAX_ARGV || envc > MAX_ENVP)
    return -EINVAL;

  int res;
  elf_program_t elf_prog = {0};
  if ((res = elf_load_file(&elf_prog, path, ET_EXEC)) < 0) {
    DPRINTF("failed to load elf file '%s' {:err}\n", path, res);
    return res;
  }

  // determine the size of the info block
  size_t info_size = 0;
  size_t argl[MAX_ARGV] = {0};
  size_t envl[MAX_ENVP] = {0};
  for (size_t i = 0; i < argc; i++) {
    argl[i] = strlen(argp[i]) + 1;
    info_size += argl[i];
  }
  for (size_t i = 0; i < envc; i++) {
    envl[i] = strlen(envp[i]) + 1;
    info_size += envl[i];
  }

  // allocate and setup the stack
  size_t stack_size = USER_STACK_SIZE + (info_size > 0 ? align(info_size, PAGE_SIZE) : 0);
  uintptr_t stack_base = (uintptr_t) vmalloc_n(stack_size, VM_WRITE | VM_STACK | VM_USER, "stack");
  if (stack_base == 0) {
    return -ENOMEM;
  }

  // copy all the strings into the stack info block (top of stack)
  void *info_base = (void *)(stack_base + stack_size - info_size);
  char *info_ptr = info_base;
  for (size_t i = 0; i < argc; i++) {
    memcpy(info_ptr, argp[i], argl[i]);
    info_ptr += argl[i];
  }
  for (size_t i = 0; i < envc; i++) {
    memcpy(info_ptr, envp[i], envl[i]);
    info_ptr += envl[i];
  }

  // setup the stack according to the linux x86_64 ABI
  // aux vectors
  uintptr_t sp = stack_base + USER_STACK_SIZE;
  // TODO: fix when process api is done
  PUSH_STACK(sp, AUX(AT_NULL, 0));
  PUSH_STACK(sp, AUX(AT_UID, 0));
  PUSH_STACK(sp, AUX(AT_GID, 0));
  PUSH_STACK(sp, AUX(AT_EUID, 0));
  PUSH_STACK(sp, AUX(AT_EGID, 0));
  PUSH_STACK(sp, AUX(AT_ENTRY, elf_prog.entry));
  if (elf_prog.interp) {
    PUSH_STACK(sp, AUX(AT_BASE, elf_prog.interp->base));
  } else {
    PUSH_STACK(sp, AUX(AT_BASE, elf_prog.base));
  }
  PUSH_STACK(sp, AUX(AT_PAGESZ, PAGE_SIZE));
  PUSH_STACK(sp, AUX(AT_PHNUM, elf_prog.phnum));
  PUSH_STACK(sp, AUX(AT_PHENT, elf_prog.phentsize));
  PUSH_STACK(sp, AUX(AT_PHDR, elf_prog.phdr));
  // environment pointers (reverse order)
  PUSH_STACK(sp, (size_t)0);
  for (size_t i = 0; i < envc; i++) {
    info_ptr -= envl[envc-i-1];
    PUSH_STACK(sp, info_ptr);
  }
  // argument pointers (reverse order)
  PUSH_STACK(sp, (size_t)0);
  for (size_t i = 0; i < argc; i++) {
    info_ptr -= argl[argc-i-1];
    PUSH_STACK(sp, info_ptr);
  }
  // argument count
  PUSH_STACK(sp, argc);

  program->base = elf_prog.base;
  program->end = elf_prog.end;
  if (elf_prog.interp) {
    program->entry = elf_prog.interp->entry;
    kfree(elf_prog.interp->path);
    kfree(elf_prog.interp);
  } else {
    program->entry = elf_prog.entry;
  }
  program->stack = vm_get_mapping(stack_base);
  program->sp = sp;
  return 0;
}
