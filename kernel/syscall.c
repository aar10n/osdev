//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <syscall.h>
#include <cpu/cpu.h>
#include <printf.h>
#include <fs.h>

extern void syscall_handler();
typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t);

#define to_syscall(func) ((syscall_t) func)


static int sys_exit(int ret) {
  kprintf(">>>> exit %d <<<<\n", ret);
  return 0;
}

static int sys_open(const char *filename, int flags, mode_t mode) {
  int fd = fs_open(filename, flags, mode);
  return fd;
}

static int sys_close(int fd) {
  int status = fs_close(fd);
  return status;
}

static ssize_t sys_read(int fd, void *buf, size_t nbytes) {
  ssize_t read = fs_read(fd, buf, nbytes);
  return read;
}

static ssize_t sys_write(int fd, void *buf, size_t nbytes) {
  ssize_t written = fs_write(fd, buf, nbytes);
  return written;
}

static off_t sys_lseek(int fd, off_t offset, int whence) {
  off_t newoff = fs_lseek(fd, offset, whence);
  return newoff;
}

// function table

static syscall_t syscalls[] = {
  [SYS_EXIT]  = to_syscall(sys_exit),
  [SYS_OPEN]  = to_syscall(sys_open),
  [SYS_CLOSE] = to_syscall(sys_close),
  [SYS_READ]  = to_syscall(sys_read),
  [SYS_WRITE] = to_syscall(sys_write),
  [SYS_LSEEK] = to_syscall(sys_lseek),
};
static int num_syscalls = sizeof(syscalls) / sizeof(void *);



void syscalls_init() {
  write_msr(IA32_LSTAR_MSR, (uintptr_t) syscall_handler);
  write_msr(IA32_SFMASK_MSR, 0);
  write_msr(IA32_STAR_MSR, 0x10LL << 48 | KERNEL_CS << 32);
}


__used int handle_syscall(
  int syscall,
  uint64_t arg0,
  uint64_t arg1,
  uint64_t arg2,
  uint64_t arg3
) {
  kprintf(">>> syscall %d <<<\n", syscall);
  if (syscall > num_syscalls - 1) {
    kprintf("[syscall] bad syscall %d\n", syscall);
    return -1;
  }

  syscall_t func = syscalls[syscall];
  uint64_t result = func(arg0, arg1, arg2, arg3);
  if (syscall == SYS_EXIT) {
    kprintf("program exited\n");
    kprintf("haulting...\n");

    while (true) {
      cpu_hlt();
    }
  }

  kprintf("result: %d\n", result);
  return result;
}
