//
// Created by Aaron Gill-Braun on 2022-07-20.
//

#ifndef KERNEL_IPI_H
#define KERNEL_IPI_H

#include <base.h>

// inter-processor interrupts and message passing

// ipi types
#define IPI_PANIC    0
#define IPI_INVLPG   1
#define IPI_SCHEDULE 2

#define NUM_IPIS 2

// ipi delivery modes
typedef enum ipi_mode {
  IPI_SELF,
  IPI_ALL_INCL,
  IPI_ALL_EXCL,
} ipi_mode_t;

typedef void (*ipi_handler_t)(uint64_t data);

int ipi_deliver_cpu_id(uint8_t type, uint8_t cpu_id, uint64_t data);
int ipi_deliver_mode(uint8_t type, ipi_mode_t mode, uint64_t data);

#endif
