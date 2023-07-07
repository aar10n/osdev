//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <kernel/syscall.h>

#include <kernel/cpu/cpu.h>

#include <kernel/sched.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/signal.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include <macros.h>

typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define SYSCALL(name, n_args, ret_type, ...) static ret_type sys_ ##name(MACRO_JOIN(__VA_ARGS__)) {};
#define PARAM(type, name) type name
#include <kernel/syscalls.def>


__used int handle_syscall(
  int syscall,
  uint64_t arg0,
  uint64_t arg1,
  uint64_t arg2,
  uint64_t arg3,
  uint64_t arg4,
  uint64_t arg5
) {
  // sys_sched_rr_get_interval()

  // // kprintf(">>> syscall %d <<<\n", syscall);
  // if (syscall > num_syscalls - 1) {
  //   kprintf("[syscall] bad syscall %d\n", syscall);
  //   return -1;
  // }
  // // kprintf(">>> %s <<<\n", syscall_names[syscall]);
  //
  // syscall_t func = syscalls[syscall];
  // if (func == NULL) {
  //   panic("not implemented");
  // }
  //
  // uint64_t result = func(arg0, arg1, arg2, arg3, arg4, arg5);
  // if (syscall == SYS_EXIT) {
  //   kprintf("program exited\n");
  //   kprintf("haulting...\n");
  //   thread_block();
  //   unreachable;
  // }

  // kprintf("result: %d\n", result);
  // return (int) result;
  return -1;
}
