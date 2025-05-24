//
// Created by Aaron Gill-Braun on 2024-01-21.
//

#include <kernel/exec.h>
#include <kernel/loadelf.h>
#include <kernel/proc.h>
#include <kernel/mm.h>
#include <kernel/fs.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <elf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("exec: " fmt, ##__VA_ARGS__)

#define AUXV_COUNT 12
#define AUX(type, val) ((Elf64_auxv_t) { .a_type = (type), .a_un.a_val = (val) })

static inline int exec_type_to_et(enum exec_type type) {
  switch (type) {
    case EXEC_BIN: return ET_EXEC;
    case EXEC_DYN: return ET_DYN;
    default: unreachable;
  }
}

static int exec_map_file_full(int fd, void **file_base, size_t *file_size) {
  int res;
  struct stat stat;
  if ((res = fs_fstat(fd, &stat)) < 0) {
    return res;
  }

  size_t size = page_align(stat.st_size);
  void *addr = vm_mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    return -ENOMEM;
  }

  *file_base = addr;
  *file_size = size;
  return 0;
}

//

int exec_load_image(enum exec_type type, uintptr_t base, cstr_t path, __out struct exec_image **imagep) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    DPRINTF("failed to open file '%s' {:err}\n", path, fd);
    return fd;
  }

  int res;
  void *file_base = NULL;
  size_t file_size = 0;
  if ((res = exec_map_file_full(fd, &file_base, &file_size)) < 0) {
    fs_close(fd);
    return res;
  }

  // determine the kind of file is being exec'd and call the right loader
  int etype = exec_type_to_et(type);
  struct exec_image *image = kmallocz(sizeof(struct exec_image));
  image->type = type;
  image->path = str_from_cstr(path);
  if (elf_is_valid_file(file_base, file_size)) {
    res = elf_load_image(fd, file_base, file_size, etype, base, image);
  } else {
    // TODO: support scripts with shebangs
    DPRINTF("invalid executable file '%s' {:err}\n", path, res);
    res = -ENOEXEC;
  }

  if (res < 0)
    exec_free_image(&image);
  else
    *imagep = image;

  if (vmap_free((uintptr_t) file_base, file_size) < 0)
    DPRINTF("failed to free file mapping\n");

  fs_close(fd);
  return res;
}

int exec_image_setup_stack(
  struct exec_image *image,
  uintptr_t stack_base,
  size_t stack_size,
  struct pcreds *creds,
  struct pstrings *args,
  struct pstrings *env,
  struct exec_stack **stackp
) {
  // The exec stack is comprised of three different sections:
  //   1) the environment strings
  //   2) the argument strings
  //   3) the sysv abi stack (auxv entries, envp, argp pointers and argc)
  //
  // The env and arg pages contain the actual strings that are pointed to by
  // the envp and argv pointers. These pages are cow copies of the pstrings pages.
  // None of the stack pages are mapped into the current address space, rather we
  // create vm descriptors and map them later on.
  //
  // ^^ higher addresses ^^
  //        ------- cow stack pages end -------
  //        env pages         N*page_size
  //        args pages        M*page_size
  //        ------ cow stack pages start ------ <- stack_base + stack_size
  //        path string       strlen(path)+1
  //                    ...
  //        null auxv         2*uint64_t
  //        auxv entries...   2*uint64_t each [AUXV_COUNT-1 max]
  //        null              uint64_t
  //        env pointers...   uint64_t each
  //        null              uint64_t
  //        arg pointers...   uint64_t each
  // rsp -> argc              uint64_t
  //                    ...
  //        ----- owned stack pages start ----- <- stack_base
  page_t *stack_pages = alloc_pages(stack_size / PAGE_SIZE);
  if (stack_pages == NULL) {
    return -ENOMEM;
  }

  uintptr_t stack_top = stack_base + stack_size;
  size_t stack_off = stack_size;
  uint64_t arg_ptrs[ARG_MAX+2] = {0}; // +1 for argv[0]
  uint64_t env_ptrs[ARG_MAX+1] = {0};
  LIST_HEAD(vm_desc_t) descs = {0};

  // add a descriptor for the stack
  int stack_vm_flags = VM_STACK | VM_RDWR | ((stack_base != 0) ? VM_FIXED : 0);
  SLIST_ADD(&descs, vm_desc_alloc(VM_TYPE_PAGE, stack_base, stack_size, stack_vm_flags, "stack", getref(stack_pages)), next);

  // copy in the argv[0] string
  kio_t kio = kio_readonly_from_str(image->path);
  stack_off -= align(str_len(image->path) + 1, sizeof(uint64_t));
  rw_unmapped_pages(stack_pages, stack_off, &kio);

  // argv[0] pointer
  arg_ptrs[0] = stack_base + stack_off;
  // arg[1..N] pointers
  size_t args_size = 0;
  if (args->count > 0) {
    ASSERT(args->count <= ARG_MAX);
    args_size = page_align(args->size);

    size_t arg_off = 0;
    uintptr_t uptr_base = stack_base + stack_size;
    for (size_t i = 0; i < args->count; i++) {
      arg_ptrs[i+1] = uptr_base + arg_off;
      arg_off = strlen(args->kptr+arg_off) + 1;
    }

    // add a descriptor for the arg string page
    page_t *arg_pages = alloc_cow_pages(args->pages);
    SLIST_ADD(&descs, vm_desc_alloc(VM_TYPE_PAGE, stack_top, args_size, VM_READ, "arg", moveref(arg_pages)), next);
  }
  arg_ptrs[args->count+1] = 0; // null pointer

  // env pointers
  if (env->count > 0) {
    ASSERT(env->count <= ARG_MAX);
    size_t env_size = page_align(env->size);

    size_t env_off = 0;
    uintptr_t uptr_base = stack_base + stack_size + args_size;
    for (size_t i = 0; i < args->count; i++) {
      env_ptrs[i] = uptr_base + env_off;
      env_off = strlen(env->kptr+env_off) + 1;
    }

    // add a descriptor for the env string page
    page_t *env_pages = alloc_cow_pages(env->pages);
    SLIST_ADD(&descs, vm_desc_alloc(VM_TYPE_PAGE, stack_top + args_size, env_size, VM_READ, "env", moveref(env_pages)), next);
  }
  env_ptrs[env->count] = 0; // null pointer

  // auxv entries
  uintptr_t aux_base = image->base;
  if (image->interp) {
    aux_base = image->interp->base;
  }
  Elf64_auxv_t auxv[AUXV_COUNT] = {
    AUX(AT_PHDR, image->phdr),
    AUX(AT_PHNUM, image->phnum),
    AUX(AT_PHENT, sizeof(Elf64_Phdr)),
    AUX(AT_BASE, aux_base),
    AUX(AT_ENTRY, image->entry),
    AUX(AT_PAGESZ, PAGE_SIZE),
    AUX(AT_UID, creds->uid),
    AUX(AT_EUID, creds->euid),
    AUX(AT_GID, creds->gid),
    AUX(AT_EGID, creds->egid),
    AUX(AT_NULL, 0),
    AUX(AT_NULL, 0),
  };

  // copy in the auxv entries
  kio = kio_new_readable(auxv, sizeof(auxv));
  stack_off -= sizeof(auxv);
  rw_unmapped_pages(stack_pages, stack_off, &kio);

  // copy in the env pointers
  kio = kio_new_readable(env_ptrs, (env->count+1) * sizeof(uint64_t));
  stack_off -= (env->count+1) * sizeof(uint64_t);
  rw_unmapped_pages(stack_pages, stack_off, &kio);

  // copy in the arg pointers
  kio = kio_new_readable(arg_ptrs, (args->count+1) * sizeof(uint64_t));
  stack_off -= (args->count+1) * sizeof(uint64_t);
  rw_unmapped_pages(stack_pages, stack_off, &kio);

  // copy in argc
  uint64_t argc = args->count + 1; // +1 for argv[0]
  kio = kio_new_readable(&argc, sizeof(uint64_t));
  stack_off -= sizeof(uint64_t);
  rw_unmapped_pages(stack_pages, stack_off, &kio);

  struct exec_stack *stack = kmallocz(sizeof(struct exec_stack));
  stack->base = stack_base;
  stack->size = stack_size;
  stack->off = stack_off;
  stack->pages = moveref(stack_pages);
  stack->descs = LIST_FIRST(&descs);

  *stackp = stack;
  return 0;
}

int exec_free_image(struct exec_image **imagep) {
  struct exec_image *image = *imagep;
  if (image == NULL)
    return 0;

  if (image->interp)
    exec_free_image(&image->interp);
  str_free(&image->path);
  vm_desc_free_all(&image->descs);

  kfree(image);
  *imagep = NULL;
  return 0;
}

int exec_free_stack(struct exec_stack **stackp) {
  struct exec_stack *stack = *stackp;
  if (stack == NULL)
    return 0;

  vm_desc_free_all(&stack->descs);
  drop_pages(&stack->pages);
  kfree(stack);
  *stackp = NULL;
  return 0;
}
