//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <kernel/process.h>

#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/signal.h>
#include <kernel/loader.h>
#include <kernel/fs.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <bitmap.h>
#include <atomic.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>
#include <kernel/vfs/file.h>

// #define PROCESS_DEBUG
#ifdef PROCESS_DEBUG
#define proc_trace_debug(str, args...) kprintf("[process] " str "\n", ##args)
#else
#define proc_trace_debug(str, args...)
#endif

static bitmap_t *pid_nums = NULL;
static spinlock_t pid_nums_lock;
static process_t **ptable = NULL;
static size_t ptable_size = 0;

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

pid_t alloc_pid() {
  if (pid_nums == NULL) {
    pid_nums = create_bitmap(MAX_PROCS);
  }

  spin_lock(&pid_nums_lock);
  index_t pid = bitmap_get_set_free(pid_nums);
  spin_unlock(&pid_nums_lock);
  kassert(pid >= 0);
  return (pid_t) pid;
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
  process->address_space = PERCPU_ADDRESS_SPACE;

  process->num_threads = 1;
  process->uid = -1;
  process->gid = -1;
  process->pwd = fs_root_getref();
  process->files = ftable_alloc();
  spin_init(&process->lock);

  thread_t *main = thread_alloc(0, start_routine, arg, false);
  main->process = process;
  const char *func_name = debug_function_name((uintptr_t) start_routine);
  if (func_name != NULL) {
    // duplicate it because debug_function_name returns a non-owning string
    main->name = strdup(func_name);
  }

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

void process_create_root(void (function)()) {
  pid_t pid = alloc_pid();
  process_t *process = process_alloc(pid, -1, root_process_wrapper, function);

  // allocate process table
  ptable = kmallocz(sizeof(process_t *) * MAX_PROCS);
  ptable[0] = process;
  ptable_size = 1;
}

pid_t process_create(void (start_routine)()) {
  return process_create_1(start_routine, NULL);
}

pid_t process_create_1(void (start_routine)(), void *arg) {
  process_t *parent = PERCPU_PROCESS;
  pid_t pid = alloc_pid();
  proc_trace_debug("creating process %d | parent %d", pid, parent->pid);
  process_t *process = process_alloc(pid, parent->pid, (void *) start_routine, arg);
  ptable[process->pid] = process;
  atomic_fetch_add(&ptable_size, 1);

  sched_add(process->main);
  return process->pid;
}

pid_t process_fork() {
  kprintf("[process] creating process\n");
  process_t *parent = PERCPU_PROCESS;
  thread_t *parent_thread = PERCPU_THREAD;

  process_t *process = kmalloc(sizeof(process_t));
  process->pid = alloc_pid();
  process->ppid = parent->pid;
  process->address_space = fork_address_space();
  process->pwd = parent->pwd;
  process->files = ftable_alloc(); // TODO: copy file table
  kprintf("process: warning - file table not copied\n");

  // clone main thread
  thread_t *main = thread_copy(parent_thread);

  uintptr_t stack = main->kernel_stack->address;
  vm_mapping_t *vm = vm_get_mapping(stack);
  kassert(vm != NULL);

  uintptr_t frame = (uintptr_t) __builtin_frame_address(0);
  uintptr_t offset = frame - (stack + vm->size);

  uintptr_t rsp = stack + vm->size - offset;
  main->ctx->rsp = rsp;
  kprintf("[process] rsp: %p\n", rsp);

  process->main = main;
  LIST_ADD_FRONT(&process->threads, main, list);

  ptable[process->pid] = process;
  atomic_fetch_add(&ptable_size, 1);

  sched_add(main);
  return process->pid;
}

int process_execve(const char *path, char *const argv[], char *const envp[]) {
  elf_program_t prog;
  memset(&prog, 0, sizeof(elf_program_t));
  if (elf_load_file(path, &prog) < 0) {
    kprintf("error: %s\n", strerror(ERRNO));
    return -1;
  }

  if (prog.linker == NULL) {
    panic("exec: program is not linked with libc");
  }

  thread_t *thread = PERCPU_THREAD;
  if (thread->user_stack == NULL) {
    thread_alloc_stack(thread, true); // allocate user stack
    memset((void *) thread->user_stack->address, 0, USER_STACK_SIZE);
  }
  uintptr_t stack_top = thread->user_stack->address + USER_STACK_SIZE;
  uint64_t *rsp = (void *) stack_top;

  int argc = ptr_list_len((void *) argv);
  int envc = ptr_list_len((void *) envp);

  uint64_t *argv_remap = kmalloc(argc * sizeof(uint64_t));
  for (int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]);
    rsp -= len + 1;
    memcpy((void *) rsp, argv[i], len + 1);
    argv_remap[i] = (uint64_t) rsp;
  }

  uint64_t *envp_remap = kmalloc(envc * sizeof(uint64_t));
  for (int i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]);
    rsp -= len + 1;
    memcpy((void *) rsp, envp[i], len + 1);
    envp_remap[i] = (uint64_t) rsp;
  }

  // // AT_NULL
  // rsp -= 1;
  // *rsp = AT_NULL;
  // // AT_ENTRY
  // rsp -= 2;
  // *((auxv_t *) rsp) = (auxv_t){ AT_ENTRY, prog.entry };
  // // AT_PHENT
  // rsp -= 2;
  // *((auxv_t *) rsp) = (auxv_t){ AT_PHENT, prog.phent };
  // // AT_PHNUM
  // rsp -= 2;
  // *((auxv_t *) rsp) = (auxv_t){ AT_PHNUM, prog.phnum };
  // // AT_PHDR
  // rsp -= 2;
  // *((auxv_t *) rsp) = (auxv_t){ AT_PHDR, prog.phdr };

  // zero
  rsp -= 1;
  *rsp = 0;
  // environment pointers
  if (envp) {
    for (int i = envc; i > 0; i--) {
      rsp -= 1;
      *rsp = (uint64_t) envp_remap[i - 1];
    }
    kfree(envp_remap);
  }
  // zero
  rsp -= 1;
  *rsp = 0;
  // argument pointers
  if (argv) {
    for (int i = argc; i > 0; i--) {
      rsp -= 1;
      *rsp = (uint64_t) argv_remap[i - 1];
    }
    kfree(argv_remap);
  }
  // argument count
  rsp -= 1;
  *rsp = argc;

  thread->user_sp = (uintptr_t) rsp;
  // vm_print_debug_address_space();

  // panic("ready for userspace!");
  sysret((uintptr_t) prog.linker->entry, (uintptr_t) rsp);
}

pid_t getpid() {
  return PERCPU_PROCESS->pid;
}

pid_t getppid() {
  return PERCPU_PROCESS->ppid;
}

id_t gettid() {
  return PERCPU_THREAD->tid;
}

uid_t getuid() {
  return PERCPU_PROCESS->uid;
}

gid_t getgid() {
  return PERCPU_PROCESS->gid;
}

//

process_t *process_get(pid_t pid) {
  if (ptable == NULL) {
    return NULL;
  }

  if (pid >= MAX_PROCS || pid < 0) {
    return NULL;
  }
  return ptable[pid];
}

thread_t *process_get_sigthread(process_t *process, int sig) {
  kassert(sig > 0 && sig < NSIG);

  thread_t *handler = process->sig_threads[sig];
  if (handler) {
    if (!sig_masked(handler, sig)) {
      return handler;
    }
    process->sig_threads[sig] = NULL;
  }

  handler = LIST_FIRST(&process->threads);
  while (handler != NULL) {
    if (!sig_masked(handler, sig)) {
      process->sig_threads[sig] = handler;
      return handler;
    }
    handler = LIST_NEXT(handler, group);
  }
  return NULL;
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

void proc_print_thread_stats(process_t *proc) {
  kassert(proc != NULL);

  kprintf("proc: process %d(%d) | CPU#%d\n", proc->pid, proc->ppid, PERCPU_ID);
  thread_t *thread;
  LIST_FOREACH(thread, &proc->threads, group) {
    sched_stats_t *stats = thread->stats;
    kprintf("\t--> thread %d\n", thread->tid);
    kprintf("\t\ttotal_time=%llu, sched_count=%zu, preempt_count=%zu, sleep_count=%zu, yield_count=%zu\n",
            stats->total_time, stats->sched_count, stats->preempt_count, stats->sleep_count, stats->yield_count);
  }
}
