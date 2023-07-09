//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <kernel/syscall.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include <abi/dirent.h>
#include <abi/poll.h>
#include <abi/resource.h>
#include <abi/stat.h>
#include <abi/statfs.h>
#include <abi/select.h>
#include <abi/signal.h>
#include <abi/sysinfo.h>
#include <abi/time.h>
#include <abi/utsname.h>

typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)


#define PARAM(type, name) type name
#define SYSCALL(name, n_args, ret_type, ...) \
  ret_type weak sys_ ##name(MACRO_JOIN(__VA_ARGS__)) { \
    panic("syscall not implemented: " #name " (%d)", SYS_ ##name); \
  };
#include <kernel/syscalls.def>

#define PARAM(type, name)
#define SYSCALL(name, n_args, ret_type, ...) [SYS_ ##name] = (void *) sys_ ##name,
static syscall_t syscall_handlers[] = {
#include <kernel/syscalls.def>
};

#define PARAM(type, name)
#define SYSCALL(name, n_args, ret_type, ...) [SYS_ ##name] = #name,
static const char *syscall_names[] = {
#include <kernel/syscalls.def>
};

__used int handle_syscall(int n, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
  if (n < 0 || n > SYS_MAX) {
    DPRINTF("!!! bad syscall %d !!!\n", n);
    // TODO: exit?
    return -1;
  }

  syscall_t fn = syscall_handlers[n];
  if (!fn) {
    DPRINTF("syscall not implemented: %d\n", n);
    return -ESYSNOTIMPLM;
  }

  DPRINTF("syscall: %s\n", syscall_names[n]);
  return (int) fn(a0, a1, a2, a3, a4, a5);
}
