//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <base.h>
#include <queue.h>
#include <mutex.h>

#define MAX_PROCS       1024
#define DEFAULT_RFLAGS  0x246

#define PROCESS_LOCK(proc) (spin_lock(&(proc)->lock))
#define PROCESS_UNLOCK(proc) (spin_unlock(&(proc)->lock))

typedef struct thread thread_t;
typedef struct address_space address_space_t;
typedef struct dentry dentry_t;
typedef struct signal signal_t;
typedef struct sig_handler sig_handler_t;
typedef struct message message_t;

typedef struct process {
  pid_t pid;                      // process id
  pid_t ppid;                     // parent pid
  address_space_t *address_space; // address space
  // !!! DO NOT CHANGE ABOVE HERE !!!
  // assembly code in thread.asm accesses these fields using known offsets

  uid_t uid;                      // user id
  gid_t gid;                      // group id
  dentry_t **pwd;                 // process working directory
  void *files;                    // open file table
  size_t num_threads;             // number of threads
  spinlock_t lock;                // process lock

  mutex_t sig_mutex;              // signal mutex
  sig_handler_t **sig_handlers;   // signal handlers
  thread_t **sig_threads;         // signal handling threads

  mutex_t ipc_mutex;              // ipc mutex
  cond_t ipc_cond_avail;          // ipc message available
  cond_t ipc_cond_recvd;          // ipc message received
  message_t *ipc_msg;             // ipc message buffer

  thread_t *main;                 // main thread
  LIST_HEAD(thread_t) threads;    // process threads (group)
  LIST_HEAD(struct process) list; // process list
} process_t;

void process_create_root(void (function)());

pid_t process_create(void (start_routine)());
pid_t process_create_1(void (start_routine)(), void *arg);
pid_t process_fork();
int process_execve(const char *path, char *const argv[], char *const envp[]);

pid_t getpid();
pid_t getppid();
id_t gettid();
uid_t getuid();
gid_t getgid();

process_t *process_get(pid_t pid);
thread_t *process_get_sigthread(process_t *process, int sig);

void proc_print_thread_stats(process_t *proc);

#endif
