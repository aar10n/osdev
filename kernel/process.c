//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <process.h>
#include <scheduler.h>
#include <string.h>
#include <atomic.h>
#include <thread.h>

#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <fs.h>
#include <file.h>
#include <loader.h>

#include <printf.h>
#include <panic.h>

#define PROCESS_DEBUG
#ifdef PROCESS_DEBUG
#define proc_trace_debug(str, args...) kprintf("[process] " str "\n", ##args)
#else
#define proc_trace_debug(str, args...)
#endif

static uint64_t __pid = 0;

static inline int ptr_list_len(const uintptr_t *list) {
  if (list == NULL) {
    return 0;
  }

  int count = 0;
  while (*list) {
    list++;
    count++;
  }
  return count;
}

uint64_t alloc_pid() {
  return atomic_fetch_add(&__pid, 1);
}

noreturn void *root_process_wrapper(void *root_fn) {
  void (*fn)() = root_fn;
  fn();
  while (true) {
    cpu_pause();
  }
}

//

process_t *process_alloc(pid_t pid, pid_t ppid, void *(start_routine)(void *), void *arg) {
  process_t *process = kmalloc(sizeof(process_t));
  memset(process, 0, sizeof(process_t));

  process->pid = pid;
  process->ppid = ppid;
  process->vm = VM;

  process->uid = -1;
  process->gid = -1;
  process->pwd = &fs_root;
  process->files = create_file_table();

  thread_t *main = thread_alloc(0, start_routine, arg);
  main->process = process;
  process->main = main;
  LIST_INIT(&process->threads);
  LIST_INIT(&process->list);
  LIST_ADD_FRONT(&process->threads, main, group);
  return process;
}

void process_free(process_t *process) {
  thread_t *thread = process->main;
  while (thread) {
    thread_t *next = LIST_NEXT(thread, group);
    thread_free(thread);
    thread = next;
  }

  // free file table
  // free vm
  kfree(process);
}

//

process_t *create_root_process(void (function)()) {
  pid_t pid = alloc_pid();
  process_t *process = process_alloc(pid, -1, root_process_wrapper, function);
  return process;
}

//

pid_t process_create(void (start_routine)()) {
  return process_create_1(start_routine, NULL);
}

pid_t process_create_1(void (start_routine)(), void *arg) {
  process_t *parent = current_process;
  pid_t pid = alloc_pid();
  proc_trace_debug("creating process %d | parent %d", pid, parent->pid);
  process_t *process = process_alloc(pid, parent->pid, (void *) start_routine, arg);
  scheduler_add(process->main);
  return process->pid;
}

pid_t process_fork() {
  kprintf("[process] creating process\n");
  process_t *parent = current_process;
  thread_t *parent_thread = current_thread;

  process_t *process = kmalloc(sizeof(process_t));
  process->pid = alloc_pid();
  process->ppid = parent->pid;
  process->pwd = parent->pwd;
  process->files = copy_file_table(parent->files);
  process->vm = vm_duplicate();

  // clone main thread
  thread_t *main = thread_copy(parent_thread);

  uintptr_t stack = main->stack->addr;
  uintptr_t frame = (uintptr_t) __builtin_frame_address(0);
  uintptr_t offset = frame - STACK_VA;
  uintptr_t rsp = stack + STACK_SIZE - offset;
  main->ctx->rsp = rsp;
  kprintf("[process] rsp: %p\n", rsp);

  process->main = main;
  LIST_ADD_FRONT(&process->threads, main, list);

  scheduler_add(main);
  return process->pid;
}

int process_execve(const char *path, char *const argv[], char *const envp[]) {
  elf_program_t prog;
  memset(&prog, 0, sizeof(elf_program_t));
  if (load_elf_file(path, &prog) < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return -1;
  }
  kassert(prog.linker != NULL);

  size_t stack_size = SIZE_TO_PAGES(THREAD_STACK_SIZE);
  page_t *stack_pages = alloc_zero_pages(stack_size, PE_USER | PE_WRITE);
  uintptr_t stack_top = stack_pages->addr + THREAD_STACK_SIZE;
  uint64_t *rsp = (void *) stack_top;

  int argc = ptr_list_len((void *) argv);
  int envc = ptr_list_len((void *) envp);

  // AT_NULL
  rsp -= 1;
  *rsp = AT_NULL;
  // AT_ENTRY
  rsp -= 2;
  *((auxv_t *) rsp) = (auxv_t){ AT_ENTRY, prog.entry };
  // AT_PHENT
  rsp -= 2;
  *((auxv_t *) rsp) = (auxv_t){ AT_PHENT, prog.phent };
  // AT_PHNUM
  rsp -= 2;
  *((auxv_t *) rsp) = (auxv_t){ AT_PHNUM, prog.phnum };
  // AT_PHDR
  rsp -= 2;
  *((auxv_t *) rsp) = (auxv_t){ AT_PHDR, prog.phdr };
  // zero
  rsp -= 1;
  *rsp = 0;
  // environment pointers
  if (envp) {
    for (int i = envc; i > 0; i--) {
      rsp -= 1;
      *rsp = (uint64_t) envp[i - 1];
    }
  }
  // zero
  rsp -= 1;
  *rsp = 0;
  // argument pointers
  if (argv) {
    for (int i = argc; i > 0; i--) {
      rsp -= 1;
      *rsp = (uint64_t) argv[i - 1];
    }
  }
  // argument count
  rsp -= 1;
  *rsp = argc;

  // page_t *old_pages = current_thread->stack;
  current_thread->stack = stack_pages;
  // unmap_pages(old_pages);

  sysret((uintptr_t) prog.linker->entry, (uintptr_t) rsp);
}

pid_t getpid() {
  return current_process->pid;
}

pid_t getppid() {
  return current_process->ppid;
}

id_t gettid() {
  return current_thread->tid;
}

uid_t getuid() {
  return current_process->uid;
}

gid_t getgid() {
  return current_process->gid;
}



//

void print_debug_process(process_t *process) {
  kprintf("process:\n");
  kprintf("  pid: %d\n", process->pid);
  kprintf("  ppid: %d\n", process->ppid);

  thread_t *thread = LIST_FIRST(&process->threads);
  while (thread) {
    print_debug_thread(thread);
    thread = LIST_NEXT(thread, list);
  }
}
