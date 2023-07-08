//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/process.h>

#include <abi/signal.h>

// void signal_init_handlers(process_t *process);
// int signal_send(pid_t pid, int sig, sigval_t value);
// int signal_getaction(pid_t pid, int sig, sigaction_t *oact);
// int signal_setaction(pid_t pid, int sig, int type, const sigaction_t *act);

#endif
