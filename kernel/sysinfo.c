//
// Created by Aaron Gill-Braun on 2025-07-20.
//

#include <kernel/sysinfo.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#include <fs/procfs/procfs.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("sysinfo: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("sysinfo: %s: " fmt, __func__, ##__VA_ARGS__)

static char hostname[256] = "localhost";

static int hostname_show(seqfile_t *sf, void *data) {
  return seq_printf(sf, "%s\n", hostname);
}

static ssize_t hostname_write(seqfile_t *sf, off_t off, kio_t *kio) {
  if (off != 0) {
    return -EINVAL;
  }

  DPRINTF("hostname_write: writing to hostname\n");
  size_t len = min(kio_remaining(kio), sizeof(hostname) - 1);
  size_t nbytes = kio_read_out(hostname, len, 0, kio);
  if (nbytes == 0) {
    return (ssize_t) nbytes;
  }

  // remove trailing newline if present
  if (hostname[nbytes - 1] == '\n') {
    hostname[nbytes - 1] = '\0';
  } else {
    hostname[nbytes] = '\0';
  }
  return (ssize_t) nbytes;
}
PROCFS_REGISTER_SIMPLE(hostname, "/sys/kernel/hostname", hostname_show, hostname_write, 0644);

static int version_show(seqfile_t *sf, void *data) {
  return seq_puts(sf, "osdev 0.0.0\n");
}
PROCFS_REGISTER_SIMPLE(version, "/version", version_show, NULL, 0444);

//
// MARK: Syscalls
//

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
