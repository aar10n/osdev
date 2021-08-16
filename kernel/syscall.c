//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <syscall.h>
#include <cpu/cpu.h>
#include <printf.h>
#include <scheduler.h>
#include <process.h>
#include <thread.h>
#include <fs.h>
#include <panic.h>

extern void syscall_handler();
typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define to_syscall(func) ((syscall_t) func)


static int sys_exit(int ret) {
  kprintf(">>>> exit %d <<<<\n", ret);
  return 0;
}

static uint32_t sys_sleep(uint32_t seconds) {
  scheduler_sleep(seconds * US_PER_SEC);
  return 0;
}

static int sys_yield() {
  return scheduler_yield();
}

static void sys_set_fs_base(uintptr_t addr) {
  current_thread->tls->addr = addr;
  write_fsbase(addr);
}

static void sys_panic(const char *message) {
  panic(message);
}

static void sys_log(const char *message) {
  kprintf("%s\n", message);
}

// function table

const char *syscall_names[] = {
  [SYS_EXIT] = "SYS_EXIT",
  [SYS_EXEC] = "SYS_EXEC",
  [SYS_OPEN] = "SYS_OPEN",
  [SYS_CLOSE] = "SYS_CLOSE",
  [SYS_READ] = "SYS_READ",
  [SYS_WRITE] = "SYS_WRITE",
  [SYS_POLL] = "SYS_POLL",
  [SYS_LSEEK] = "SYS_LSEEK",
  [SYS_CREATE] = "SYS_CREATE",
  [SYS_MKNOD] = "SYS_MKNOD",
  [SYS_MKDIR] = "SYS_MKDIR",
  [SYS_LINK] = "SYS_LINK",
  [SYS_UNLINK] = "SYS_UNLINK",
  [SYS_SYMLINK] = "SYS_SYMLINK",
  [SYS_RENAME] = "SYS_RENAME",
  [SYS_READLINK] = "SYS_READLINK",
  [SYS_READDIR] = "SYS_READDIR",
  [SYS_TELLDIR] = "SYS_TELLDIR",
  [SYS_SEEKDIR] = "SYS_SEEKDIR",
  [SYS_REWINDDIR] = "SYS_REWINDDIR",
  [SYS_RMDIR] = "SYS_RMDIR",
  [SYS_CHDIR] = "SYS_CHDIR",
  [SYS_CHMOD] = "SYS_CHMOD",
  [SYS_STAT] = "SYS_STAT",
  [SYS_FSTAT] = "SYS_FSTAT",
  [SYS_SLEEP] = "SYS_SLEEP",
  [SYS_NANOSLEEP] = "SYS_NANOSLEEP",
  [SYS_YIELD] = "SYS_YIELD",
  [SYS_GETPID] = "SYS_GETPID",
  [SYS_GETPPID] = "SYS_GETPPID",
  [SYS_GETTID] = "SYS_GETTID",
  [SYS_GETUID] = "SYS_GETUID",
  [SYS_GETGID] = "SYS_GETGID",
  [SYS_GET_CWD] = "SYS_GET_CWD",
  [SYS_MMAP] = "SYS_MMAP",
  [SYS_MUNMAP] = "SYS_MUNMAP",
  [SYS_FORK] = "SYS_FORK",
  [SYS_PREAD] = "SYS_PREAD",
  [SYS_PWRITE] = "SYS_PWRITE",
  [SYS_IOCTL] = "SYS_IOCTL",
  [SYS_SET_FS_BASE] = "SYS_SET_FS_BASE",
  [SYS_PANIC] = "SYS_PANIC",
  [SYS_LOG] = "SYS_LOG",
};

static syscall_t syscalls[] = {
  [SYS_EXIT] = to_syscall(sys_exit),
  [SYS_EXEC] = to_syscall(process_execve),
  [SYS_OPEN] = to_syscall(fs_open),
  [SYS_CLOSE] = to_syscall(fs_close),
  [SYS_READ] = to_syscall(fs_read),
  [SYS_WRITE] = to_syscall(fs_write),
  [SYS_POLL] = NULL,
  [SYS_LSEEK] = to_syscall(fs_lseek),
  [SYS_CREATE] = to_syscall(fs_creat),
  [SYS_MKNOD] = to_syscall(fs_mknod),
  [SYS_MKDIR] = to_syscall(fs_mkdir),
  [SYS_LINK] = to_syscall(fs_link),
  [SYS_UNLINK] = to_syscall(fs_unlink),
  [SYS_SYMLINK] = to_syscall(fs_symlink),
  [SYS_RENAME] = to_syscall(fs_rename),
  [SYS_READLINK] = NULL,
  [SYS_READDIR] = to_syscall(fs_readdir),
  [SYS_TELLDIR] = to_syscall(fs_telldir),
  [SYS_SEEKDIR] = to_syscall(fs_seekdir),
  [SYS_REWINDDIR] = to_syscall(fs_rewinddir),
  [SYS_RMDIR] = to_syscall(fs_rmdir),
  [SYS_CHDIR] = to_syscall(fs_chdir),
  [SYS_CHMOD] = to_syscall(fs_chmod),
  [SYS_STAT] = to_syscall(fs_stat),
  [SYS_FSTAT] = to_syscall(fs_fstat),
  [SYS_SLEEP] = to_syscall(sys_sleep),
  [SYS_NANOSLEEP] = NULL,
  [SYS_YIELD] = to_syscall(sys_yield),
  [SYS_GETPID] = to_syscall(getpid),
  [SYS_GETPPID] = to_syscall(getppid),
  [SYS_GETTID] = to_syscall(gettid),
  [SYS_GETUID] = to_syscall(getuid),
  [SYS_GETGID] = to_syscall(getgid),
  [SYS_GET_CWD] = to_syscall(fs_getcwd),
  [SYS_MMAP] = to_syscall(fs_mmap),
  [SYS_MUNMAP] = to_syscall(fs_munmap),
  [SYS_FORK] = to_syscall(process_fork),
  [SYS_PREAD] = NULL,
  [SYS_PWRITE] = NULL,
  [SYS_IOCTL] = NULL,
  [SYS_SET_FS_BASE] = to_syscall(sys_set_fs_base),
  [SYS_PANIC] = to_syscall(sys_panic),
  [SYS_LOG] = to_syscall(sys_log),
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
  uint64_t arg3,
  uint64_t arg4,
  uint64_t arg5
) {
  kprintf(">>> syscall %d <<<\n", syscall);
  if (syscall > num_syscalls - 1) {
    kprintf("[syscall] bad syscall %d\n", syscall);
    return -1;
  }
  kprintf(">>> %s <<<\n", syscall_names[syscall]);

  syscall_t func = syscalls[syscall];
  uint64_t result = func(arg0, arg1, arg2, arg3, arg4, arg5);
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
