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

static inline size_t ptr_list_len(char *const list[], size_t *full_sizep) {
  if (list == NULL)
    return 0;

  size_t count = 0;
  size_t full_size = 0;
  while (list[count] != NULL) {
    full_size += strlen(list[count]) + 1;
    count++;
  }

  if (full_sizep)
    *full_sizep = full_size;
  return count;
}

static int load_file_data(int fd, void **bufp, size_t *sizep) {
  ssize_t res;
  struct stat stat;
  if ((res = fs_fstat(fd, &stat)) < 0) {
    return (int) res;
  }

  void *filebuf = vmalloc(stat.st_size, VM_RDWR|VM_USER);
  kio_t kio = kio_new_writable(filebuf, stat.st_size);
  if ((res = fs_kread(fd, &kio)) < 0) {
    vfree(filebuf);
    return (int) res;
  }

  *bufp = filebuf;
  *sizep = stat.st_size;
  return 0;
}

//

int exec_load_image(uintptr_t base, const char *path, struct exec_image **imagep) {
  int fd = fs_open(path, O_RDONLY, 0);
  if (fd < 0) {
    DPRINTF("failed to open file '%s' {:err}\n", path, fd);
    return fd;
  }

  int res;
  void *filebuf = NULL;
  size_t filesz = 0;
  if ((res = load_file_data(fd, &filebuf, &filesz)) < 0) {
    goto ret;
  }

  struct exec_image *image = kmallocz(sizeof(struct exec_image));
  if (elf_is_valid_file(filebuf, filesz)) {
    res = elf_load_image(filebuf, filesz, ET_EXEC, image);
  } else {
    // TODO: support scripts with shebangs
    DPRINTF("invalid executable file '%s' {:err}\n", path, res);
    res = -ENOEXEC;
    goto ret;
  }

LABEL(ret);
  if (res < 0)
    kfree(image);

  if (filebuf != NULL)
    vfree(filebuf);

  fs_close(fd);
  return res;
}

int exec_setup_stack(uintptr_t base, struct pargs *args, struct penv *env, struct exec_stack **stackp) {
  // The exec stack is comprised of three different sections:
  //   1) the environment variables
  //   2) the argument variables
  //   3) the sysv abi stack (auxv entries, envp, argp pointers and argc)
  //
  // The env and arg pages contain the actual strings that are pointed to by
  // the envp and argv pointers. These pages are cow copies of the pages from
  // pargs and penv. None of the stack pages are mapped into the address space,
  // rather we create vm descriptors that represent the required mappings and
  // can be mapped into process memory later on.
  //
  //        -------- cow stack pages ----------
  //        env pages         N*page_size
  //        args pages        M*page_size
  //        -------- owned stack pages --------
  //        null auxv         2*uint64_t
  //        auxv entries...   2*uint64_t each [15 max]
  //        null              uint64_t
  //        env pointers...   uint64_t each
  //        null              uint64_t
  //        arg pointers...   uint64_t each
  // rsp -> argc              uint64_t
  //                    ...
  //        -----------------------------------
  size_t owned_size = (args->arg_count + env->env_count + 3) * sizeof(uint64_t)
                    + (16 * sizeof(Elf64_auxv_t));

  size_t page_count = SIZE_TO_PAGES(owned_size);
  page_t *stack_pages = alloc_pages(page_count);
  if (stack_pages == NULL) {
    return -ENOMEM;
  }

  uintptr_t stack_top = base + PAGES_TO_SIZE(page_count);
  page_t *arg_pages = alloc_cow_pages(args->pages);
  page_t *env_pages = alloc_cow_pages(env->pages);

  struct exec_stack *stack = kmallocz(sizeof(struct exec_stack));
  SLIST_ADD(&stack->descs, vm_desc_alloc(base, PAGES_TO_SIZE(page_count), VM_STACK|VM_READ, NULL, moveref(stack_pages)), next);
  SLIST_ADD(&stack->descs, vm_desc_alloc(stack_top, page_align(args->arg_size), VM_COW|VM_READ, "args", moveref(arg_pages)), next);
  SLIST_ADD(&stack->descs, vm_desc_alloc(stack_top+page_align(args->arg_size), page_align(env->env_size), VM_COW|VM_READ, "env", moveref(env_pages)), next);

  stack->base = base;

  stack->off = stack_top;

  // the original (non-cow) arg and env pages should be mapped into kernel memory
  // so lets figure out the offsets of each string and then compute the pointers
  // to each string from the stack base.
  size_t arg_offs[args->arg_count];
  size_t env_offs[env->env_count];
  size_t arg_size = 0;
  size_t env_size = 0;
  for (size_t i = 0; i < args->arg_count; i++) {
    arg_offs[i] = arg_size;
    // arg_size += strlen(args->args[i]) + 1;
  }

  return 0;
}

int exec_free_image(struct exec_image **imagep) {
  struct exec_image *image = *imagep;
  if (image == NULL)
    return 0;

  str_free(&image->path);
  str_free(&image->interp_path);
  drop_pages(&image->pages);

  kfree(image);
  *imagep = NULL;
  return 0;
}

int exec_free_stack(struct exec_stack **stackp) {
  struct exec_stack *stack = *stackp;
  if (stack == NULL)
    return 0;

  drop_pages(&stack->pages);
  kfree(stack);
  *stackp = NULL;
  return 0;
}
