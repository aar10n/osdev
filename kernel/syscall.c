//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <kernel/syscall.h>
#include <kernel/cpu/trapframe.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include <abi/dirent.h>
#include <abi/fcntl.h>
#include <abi/poll.h>
#include <abi/resource.h>
#include <abi/sched.h>
#include <abi/stat.h>
#include <abi/statfs.h>
#include <abi/select.h>
#include <abi/signal.h>
#include <abi/sysinfo.h>
#include <abi/time.h>
#include <abi/wait.h>
#include <abi/utsname.h>
#include <sys/socket.h>

typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

extern void sys_rt_sigreturn_impl(struct trapframe *frame);

#define ASSERT(x) kassert(x)
#define LOG_TAG syscall
#include <kernel/log.h>


#define PARAM(type, name, fmt) type name
#define SYSCALL(name, n_args, ret_type, ...) \
  ret_type _weak sys_ ##name(MACRO_JOIN(__VA_ARGS__)) { \
    panic("syscall not implemented: " #name " (%d)", SYS_ ##name); \
  };
#include <kernel/syscalls.def>

#define PARAM(type, name, fmt)
#define SYSCALL(name, n_args, ret_type, ...) [SYS_ ##name] = (void *) sys_ ##name,
static syscall_t syscall_handlers[] = {
#include <kernel/syscalls.def>
};

#define PARAM(type, name, fmt)
#define SYSCALL(name, n_args, ret_type, ...) [SYS_ ##name] = #name,
static const char *syscall_names[] = {
#include <kernel/syscalls.def>
};

_used uint64_t handle_syscall(uint64_t syscall, struct trapframe *frame) {
  __assert_stack_is_aligned();
  if (syscall > SYS_MAX) {
    DPRINTF("!!! invalid syscall: %lu !!!\n", syscall);
    return -ENOSYS;
  }

  // rt_sigreturn restores the full register context from the sigframe,
  // bypassing the normal syscall return path's sign-extension
  if (syscall == SYS_rt_sigreturn) {
    sys_rt_sigreturn_impl(frame);
    return frame->rax;
  }

  syscall_t fn = syscall_handlers[syscall];
  if (!fn) {
    panic("!!! syscall not implemented: %lu !!! \n", syscall);
    return -ENOSYS;
  }
  DPRINTF("syscall: %s\n", syscall_names[syscall]);

  // rdi   arg1
  // rsi   arg2
  // rdx   arg3
  // r10   arg4
  // r8    arg5
  // r9    arg6
  return fn(frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);
}
