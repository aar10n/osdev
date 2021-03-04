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
  pid_t pid;
  pid_t ppid;
  context_t *ctx;
  uint8_t cpu_id;
  uint8_t policy;
  uint8_t priority;
  proc_status_t status;
  uid_t uid;
  gid_t gid;
  fs_node_t *pwd;
  file_table_t *files;
  vm_t *vm;
  struct {
    clock_t last_run_start;
    clock_t last_run_end;
    clock_t run_time;
    clock_t idle_time;
    clock_t sleep_time;
    uint64_t run_count;
    uint64_t block_count;
    uint64_t sleep_count;
    uint64_t yield_count;
  } stats;

  struct process *next;
  struct process *prev;
} process_t;

process_t *kthread_create(void (*func)());
process_t *kthread_create_idle(void (*func)());
pid_t process_fork(bool user);

pid_t getpid();
pid_t getppid();

void print_debug_process(process_t *process);

#endif
