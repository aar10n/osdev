//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <base.h>
#include <fs.h>

#define DEFAULT_RFLAGS 0x246
#define PROC_STACK_SIZE 0x2000

typedef enum {
  PROC_READY,
  PROC_RUNNING,
  PROC_BLOCKED,
  PROC_SLEEPING,
  PROC_KILLED
} proc_status_t;

typedef struct vm vm_t;
typedef struct thread thread_t;
typedef struct file_table file_table_t;

typedef struct {
  uint64_t rax;    // 0x00
  uint64_t rbx;    // 0x00
  uint64_t rbp;    // 0x08
  uint64_t r12;    // 0x10
  uint64_t r13;    // 0x18
  uint64_t r14;    // 0x20
  uint64_t r15;    // 0x28
  //
  uint64_t rip;    // 0x30
  uint64_t cs;     // 0x38
  uint64_t rflags; // 0x40
  uint64_t rsp;    // 0x48
  uint64_t ss;     // 0x50
} context_t;

typedef struct process {
  pid_t pid;           // process id
  pid_t ppid;          // parent pid
  vm_t *vm;            // virtual memory space

  uid_t uid;           // user id
  gid_t gid;           // group id
  dentry_t **pwd;      // process working directory
  file_table_t *files; // open file table

  thread_t *main;      // main thread (first)
  thread_t *threads;   // pointer to thread group

  struct process *next;
  struct process *prev;
} process_t;

process_t *create_root_process(void (function)());
pid_t process_create(void (start_routine)());
pid_t process_create_1(void (start_routine)(), void *arg);
pid_t process_fork();
int process_execve(const char *path, char *const argv[], char *const envp[]);

pid_t getpid();
pid_t getppid();
id_t gettid();
uid_t getuid();
gid_t getgid();

void print_debug_process(process_t *process);

#endif
