//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/mm_types.h>

#define MAX_PROCS       1024
#define DEFAULT_RFLAGS  0x246

#define PROCESS_LOCK(proc) (spin_lock(&(proc)->lock))
#define PROCESS_UNLOCK(proc) (spin_unlock(&(proc)->lock))

typedef struct thread thread_t;
typedef struct address_space address_space_t;
typedef struct ventry ventry_t;
typedef struct ftable ftable_t;

typedef struct process {
  pid_t pid;                      // process id
  pid_t ppid;                     // parent pid
  address_space_t *address_space; // address space
  // !!! DO NOT CHANGE ABOVE HERE !!!
  // assembly code in thread.asm accesses these fields using known offsets
  uid_t uid;                      // user id
  gid_t gid;                      // group id
  uid_t euid;                     // effective user id
  gid_t egid;                     // effective group id
  ventry_t *pwd;                  // working directory reference
  ftable_t *files;                // open file table
  vm_mapping_t *brk_vm;           // process brk mapping

  spinlock_t lock;                // process lock
  size_t num_threads;             // number of threads

  thread_t *main;                 // main thread
  LIST_HEAD(thread_t) threads;    // process threads (group)
  LIST_HEAD(struct process) list; // process list
} process_t;

void process_create_root(void (function)());

pid_t process_create(void (start_routine)(), str_t name);
pid_t process_create_1(void (start_routine)(), void *arg, str_t name);
pid_t process_fork();
int process_execve(const char *path, char *const argv[], char *const envp[]);

pid_t process_getpid();
pid_t process_getppid();
pid_t process_gettid();
uid_t process_getuid();
gid_t process_getgid();

process_t *process_get(pid_t pid);
// thread_t *process_get_sigthread(process_t *process, int sig);

void proc_print_thread_stats(process_t *proc);

#endif
