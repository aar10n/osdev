//
// Created by Aaron Gill-Braun on 2025-07-20.
//

#include <kernel/sysinfo.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("sysinfo: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("sysinfo: %s: " fmt, __func__, ##__VA_ARGS__)


DEFINE_SYSCALL(uname, int, struct utsname *buf) {
  DPRINTF("syscall: uname buf=%p\n", buf);
  if (vm_validate_ptr((uintptr_t) buf, /*write=*/true) < 0) {
    return -EFAULT;
  }

  // populate the utsname structure
  strlcpy(buf->sysname, "osdev", sizeof(buf->sysname));
  strlcpy(buf->nodename, "localhost", sizeof(buf->nodename));
  strlcpy(buf->release, "0.0.0", sizeof(buf->release));
  strlcpy(buf->version, "osdev 0.0.0", sizeof(buf->version)); // TODO: use actual version
  strlcpy(buf->machine, "x86_64", sizeof(buf->machine));
  strlcpy(buf->domainname, "localdomain", sizeof(buf->domainname));
  return 0;
}
