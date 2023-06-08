//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <syscall.h>

#include <cpu/cpu.h>

#include <sched.h>
#include <process.h>
#include <thread.h>
#include <signal.h>

#include <panic.h>
#include <printf.h>

extern void syscall_handler();
typedef uint64_t (*syscall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define to_syscall(func) ((syscall_t) func)
#define as_void(value) ((void *)((uint64_t)(value)))

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
  [SYS_FCNTL] = "SYS_FCNTL",
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
  [SYS_KILL] = "SYS_KILL",
  [SYS_SIGNAL] = "SYS_SIGNAL",
  [SYS_SIGACTION] = "SYS_SIGACTION",
  [SYS_DUP] = "SYS_DUP",
  [SYS_DUP2] = "SYS_DUP2",
};


static int sys_exit(int ret) {
  kprintf(">>>> exit %d <<<<\n", ret);
  return 0;
}

static int sys_execve(const char *path, char *const argv[], char *const envp[]) {
  int result = process_execve(path, argv, envp);
  if (result < 0) {
    return -ERRNO;
  }
  return result;
}

static int sys_open(const char *path, int flags, mode_t mode) {
  unimplemented("sys_open");
}

static int sys_close(int fd) {
  unimplemented("sys_close");
}

static ssize_t sys_read(int fd, void *buf, size_t nbytes) {
  unimplemented("sys_read");
}

static ssize_t sys_write(int fd, void *buf, size_t nbytes) {
  unimplemented("sys_write");
}

// int sys_poll(struct pollfd fds[], nfds_t nfds, int timeout) {
//
// }

static off_t sys_lseek(int fd, off_t offset, int whence) {
  unimplemented("sys_lseek");
}

static int sys_fnctl(int fd, int cmd, uint64_t arg) {
  unimplemented("sys_fnctl");
}

static int sys_creat(const char *path, mode_t mode) {
  unimplemented("sys_creat");
}

static int sys_mknod(const char *path, mode_t mode, dev_t dev) {
  unimplemented("sys_mknod");
}

static int sys_mkdir(const char *path, mode_t mode) {
  unimplemented("sys_mkdir");
}

static int sys_link(const char *path1, const char *path2) {
  unimplemented("sys_link");
}

static int sys_unlink(const char *path) {
  unimplemented("sys_unlink");
}

static int sys_symlink(const char *path1, const char *path2) {
  unimplemented("sys_symlink");
}

static int sys_rename(const char *oldfile, const char *newfile) {
  unimplemented("sys_rename");
}

static ssize_t sys_readlink(const char *restrict path, char *restrict buf, size_t bufsize) {
  unimplemented("sys_readlink");
}

// SYS_READDIR
// SYS_TELLDIR
// SYS_SEEKDIR
// SYS_REWINDDIR

static int sys_rmdir(const char *path) {
  unimplemented("sys_rmdir");
}

static int sys_chdir(const char *path) {
  unimplemented("sys_chdir");
}

static int sys_chmod(const char *path, mode_t mode) {
  unimplemented("sys_chmod");
}

static int sys_stat(const char *path, struct stat *statbuf) {
  unimplemented("sys_stat");
}

static int sys_fstat(int fd, struct stat *statbuf) {
  unimplemented("sys_fstat");
}

static int sys_sleep(uint32_t seconds) {
  return sched_sleep(seconds * NS_PER_SEC);
}

// SYS_NANOSLEEP

static int sys_yield() {
  return sched_yield();
}

static void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  unimplemented("sys_mmap");
}

static int sys_munmap(void *addr, size_t length) {
  unimplemented("sys_munmap");
}

static pid_t sys_fork() {
  pid_t result = process_fork();
  if (result < 0) {
    return -ERRNO;
  }
  return result;
}

// SYS_PREAD
// SYS_PWRITE
// SYS_IOCTL

static int sys_set_fs_base(uintptr_t addr) {
  PERCPU_THREAD->tls->addr = addr;
  cpu_write_fsbase(addr);
  return 0;
}

static void sys_panic(const char *message) {
  panic(message);
}

static int sys_log(const char *message) {
  // kprintf("%s\n", message);
  return 0;
}

static int sys_kill(pid_t pid, int sig) {
  int result = signal_getaction(pid, sig, NULL);
  if (result < 0) {
    return -ERRNO;
  }

  result = signal_send(pid, sig, (sigval_t){ 0 });
  if (result < 0) {
    return -ERRNO;
  }
  return 0;
}

static int sys_signal(int sig, void (*func)(int)) {
  int result = signal_getaction(PERCPU_PROCESS->pid, sig, NULL);
  if (result < 0) {
    return -ERRNO;
  }

  sigaction_t action = {
    .sa_flags = 0,
    .sa_mask = 0,
    .sa_handler = func,
  };
  result = signal_setaction(PERCPU_PROCESS->pid, sig, SIG_ACTION, &action);
  if (result < 0) {
    return -ERRNO;
  }
  return 0;
}

// SYS_SIGACTION

static int sys_dup(int fd) {
  unimplemented("sys_dup");
}

static int sys_dup2(int fd, int fd2) {
  unimplemented("sys_dup2");
}

//

static syscall_t syscalls[] = {
  [SYS_EXIT] = to_syscall(sys_exit),
  [SYS_EXEC] = to_syscall(sys_execve),
  [SYS_OPEN] = to_syscall(sys_open),
  [SYS_CLOSE] = to_syscall(sys_close),
  [SYS_READ] = to_syscall(sys_read),
  [SYS_WRITE] = to_syscall(sys_write),
  [SYS_POLL] = NULL,
  [SYS_LSEEK] = to_syscall(sys_lseek),
  [SYS_FCNTL] = to_syscall(sys_fnctl),
  [SYS_CREATE] = to_syscall(sys_creat),
  [SYS_MKNOD] = to_syscall(sys_mknod),
  [SYS_MKDIR] = to_syscall(sys_mkdir),
  [SYS_LINK] = to_syscall(sys_link),
  [SYS_UNLINK] = to_syscall(sys_unlink),
  [SYS_SYMLINK] = to_syscall(sys_symlink),
  [SYS_RENAME] = to_syscall(sys_rename),
  [SYS_READLINK] = to_syscall(sys_readlink),
  [SYS_READDIR] = NULL,
  [SYS_TELLDIR] = NULL,
  [SYS_SEEKDIR] = NULL,
  [SYS_REWINDDIR] = NULL,
  [SYS_RMDIR] = to_syscall(sys_rmdir),
  [SYS_CHDIR] = to_syscall(sys_chdir),
  [SYS_CHMOD] = to_syscall(sys_chmod),
  [SYS_STAT] = to_syscall(sys_stat),
  [SYS_FSTAT] = to_syscall(sys_fstat),
  [SYS_SLEEP] = to_syscall(sys_sleep),
  [SYS_NANOSLEEP] = NULL,
  [SYS_YIELD] = to_syscall(sys_yield),
  [SYS_GETPID] = to_syscall(getpid),
  [SYS_GETPPID] = to_syscall(getppid),
  [SYS_GETTID] = to_syscall(gettid),
  [SYS_GETUID] = to_syscall(getuid),
  [SYS_GETGID] = to_syscall(getgid),
  // [SYS_GET_CWD] = to_syscall(fs_getcwd),
  [SYS_MMAP] = to_syscall(sys_mmap),
  [SYS_MUNMAP] = to_syscall(sys_munmap),
  [SYS_FORK] = to_syscall(sys_fork),
  [SYS_PREAD] = NULL,
  [SYS_PWRITE] = NULL,
  [SYS_IOCTL] = NULL,
  [SYS_SET_FS_BASE] = to_syscall(sys_set_fs_base),
  [SYS_PANIC] = to_syscall(sys_panic),
  [SYS_LOG] = to_syscall(sys_log),
  [SYS_KILL] = to_syscall(sys_kill),
  [SYS_SIGNAL] = to_syscall(sys_signal),
  [SYS_SIGACTION] = NULL,
  [SYS_DUP] = to_syscall(sys_dup),
  [SYS_DUP2] = to_syscall(sys_dup2),
};
static int num_syscalls = sizeof(syscalls) / sizeof(void *);


void syscalls_init() {
  cpu_write_msr(IA32_LSTAR_MSR, (uintptr_t) syscall_handler);
  cpu_write_msr(IA32_SFMASK_MSR, 0);
  cpu_write_msr(IA32_STAR_MSR, 0x10LL << 48 | KERNEL_CS << 32);
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
  // kprintf(">>> syscall %d <<<\n", syscall);
  if (syscall > num_syscalls - 1) {
    kprintf("[syscall] bad syscall %d\n", syscall);
    return -1;
  }
  // kprintf(">>> %s <<<\n", syscall_names[syscall]);

  syscall_t func = syscalls[syscall];
  if (func == NULL) {
    panic("not implemented");
  }

  uint64_t result = func(arg0, arg1, arg2, arg3, arg4, arg5);
  if (syscall == SYS_EXIT) {
    kprintf("program exited\n");
    kprintf("haulting...\n");
    thread_block();
    unreachable;
  }

  // kprintf("result: %d\n", result);
  return result;
}
