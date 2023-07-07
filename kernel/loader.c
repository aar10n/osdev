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
  void *file;
  uintptr_t base;
  uintptr_t entry;

  uintptr_t phdr;
  size_t phnum;
  size_t phentsize;

  char *path;
  struct elf_program *interp;
} elf_program_t;

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("loader: " fmt, ##__VA_ARGS__)

#define AUX(t, v) ((auxv_t){ .type = (t), .value = (v) })
#define PUSH_STACK(sp, val) ((sp) -= sizeof(val), *(typeof(val) *)(sp) = (val))

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

static inline const char *elf_get_aux_type_str(size_t typ) {
  switch (typ) {
    case AT_NULL: return "AT_NULL";
    case AT_PHDR: return "AT_PHDR";
    case AT_PHENT: return "AT_PHENT";
    case AT_PHNUM: return "AT_PHNUM";
    case AT_PAGESZ: return "AT_PAGESZ";
    case AT_BASE: return "AT_BASE";
    case AT_ENTRY: return "AT_ENTRY";
    case AT_UID: return "AT_UID";
    case AT_GID: return "AT_GID";
    default: return "other";
  }
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

static const char *elf_get_interp(Elf64_Ehdr *ehdr) {
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_INTERP) {
      return offset_ptr(ehdr, phdr[i].p_offset);
    }
  }
  return NULL;
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
  size_t image_size = elf_get_loaded_memsz(ehdr, &min_vaddr);
  if (image_size == 0) {
    DPRINTF("no loadable segments\n");
    return -ENOEXEC;
  } else if (len < elf_get_image_filesz(ehdr)) {
    DPRINTF("invalid header or buffer too small\n");
    return -ENOEXEC;
  }

  min_vaddr += prog->base;
  prog->entry = prog->base + ehdr->e_entry;
  prog->phdr = offset_addr(ehdr, ehdr->e_phoff);
  prog->phentsize = ehdr->e_phentsize;
  prog->phnum = ehdr->e_phnum;

  uint32_t vm_flags = VM_WRITE | (is_user_ptr(min_vaddr) ? VM_USER : 0);
  if (ehdr->e_type == ET_EXEC)
    vm_flags |= VM_FIXED;

  // allocate and map memory for the image
  page_t *pages = alloc_pages(SIZE_TO_PAGES(image_size));
  if (pages == NULL)
    return -ENOMEM;

  vm_mapping_t *vm = vmap_pages(pages, min_vaddr, image_size, vm_flags, prog->path);
  if (vm == NULL) {
    free_pages(pages);
    return -ENOMEM;
  }

  // load each loadable segment
  Elf64_Phdr *phdr = offset_ptr(ehdr, ehdr->e_phoff);
  for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
    uint32_t typ = phdr[i].p_type;
    // if (typ == PT_DYNAMIC && e_type != ET_DYN)
    //   continue;
    // if (!(typ == PT_LOAD || typ == PT_DYNAMIC) || phdr[i].p_memsz == 0)
    //   continue;
    if (typ != PT_LOAD || phdr[i].p_memsz == 0)
      continue;

    uint64_t offset = phdr[i].p_offset;
    uint64_t filesz = phdr[i].p_filesz;
    uint64_t memsz = elf_align_up(phdr[i].p_memsz, phdr[i].p_align);
    uint64_t vaddr = prog->base + phdr[i].p_vaddr;

    uint32_t prot_flags = VM_READ;
    if (phdr[i].p_flags & PF_X)
      prot_flags |= VM_EXEC;
    if (phdr[i].p_flags & PF_W)
      prot_flags |= VM_WRITE;

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
  void *buffer = vmalloc_n(stat.st_size, VM_USER, path);
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

  elf_program_t *interp = NULL;
  const char *interp_path = elf_get_interp(buffer);
  if (interp_path != NULL) {
    // load the interpreter
    ASSERT(e_type == ET_EXEC);
    DPRINTF("interp = %s\n", interp_path);

    interp = kmallocz(sizeof(elf_program_t));
    interp->base = LIBC_BASE_ADDR;
    if ((res = elf_load_file(interp, "/initrd/lib/ld-musl-x86_64.so.1", ET_DYN)) < 0) {
      DPRINTF("failed to load interpreter '%s' {:err}\n", interp_path, res);
      kfree(interp);
      goto ret;
    }
    prog->interp = interp;
  }

  if (e_type == ET_EXEC) {
    // the interpreter needs to finish linking the executable so we need
    // to keep the file in memory and tell the interpreter where it is
    prog->file = buffer;
  } else {
    vfree(buffer);
    prog->file = NULL;
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
  uintptr_t stack_base = (uintptr_t) vmalloc(stack_size, VM_WRITE | VM_STACK | VM_USER);
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
  PUSH_STACK(sp, AUX(AT_NULL, 0));
  PUSH_STACK(sp, AUX(AT_GID, getgid()));
  PUSH_STACK(sp, AUX(AT_UID, getuid()));
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

  // {
  //   size_t *p = (void *) sp;
  //
  //   int c = (int) *p;
  //   char **argv = (void *)(p+1);
  //
  //   int i = 0;
  //   for (i=c+1; argv[i]; i++);
  //   auxv_t *auxv = (void *)(argv+i+1);
  // }

  program->stack = vm_get_mapping(stack_base);
  program->sp = sp;
  if (elf_prog.interp) {
    program->entry = elf_prog.interp->entry;
    kfree(elf_prog.interp->path);
    kfree(elf_prog.interp);
  } else {
    program->entry = elf_prog.entry;
  }
  return 0;
}
