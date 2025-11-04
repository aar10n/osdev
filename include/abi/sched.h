//
// Created by Aaron Gill-Braun on 2025-10-18.
//

#ifndef ABI_SCHED_H
#define ABI_SCHED_H

typedef struct cpu_set_t {
  unsigned long __bits[128/sizeof(long)];
} cpu_set_t;

#define __CPU_op_S(i, size, set, op) ( (i)/8U >= (size) ? 0 : \
  (((unsigned long *)(set))[(i)/8/sizeof(long)] op (1UL<<((i)%(8*sizeof(long))))) )

#define CPU_SET_S(i, size, set) __CPU_op_S(i, size, set, |=)
#define CPU_CLR_S(i, size, set) __CPU_op_S(i, size, set, &=~)
#define CPU_ISSET_S(i, size, set) __CPU_op_S(i, size, set, &)

#define CPU_SET(i, set) CPU_SET_S(i,sizeof(cpu_set_t),set)
#define CPU_CLR(i, set) CPU_CLR_S(i,sizeof(cpu_set_t),set)
#define CPU_ISSET(i, set) CPU_ISSET_S(i,sizeof(cpu_set_t),set)

#define MEMBARRIER_CMD_QUERY                        0
#define MEMBARRIER_CMD_GLOBAL                       (1 << 0)
#define MEMBARRIER_CMD_GLOBAL_EXPEDITED             (1 << 1)
#define MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED    (1 << 2)
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED            (1 << 3)
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED   (1 << 4)
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE  (1 << 5)
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE (1 << 6)
#define MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ       (1 << 7)
#define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ (1 << 8)
#define MEMBARRIER_CMD_SHARED                       MEMBARRIER_CMD_GLOBAL

#define MEMBARRIER_CMD_FLAG_CPU                     (1 << 0)

#endif
