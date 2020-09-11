//
// Created by Aaron Gill-Braun on 2020-09-08.
//

#ifndef INITRD_COMMON_H
#define INITRD_COMMON_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "fs.h"

extern uint32_t block_size;
extern uint32_t block_count;
extern char *out_file;
extern bool quiet;
extern uint32_t reserved;
extern bool verbose;

extern uint16_t last_id;

// print priority
#define QUIET 0
#define NORMAL 1
#define VERBOSE 2

#define print(priority, fmt, args...)   \
  ((priority == QUIET) ||               \
   (priority == NORMAL && !quiet) ||    \
   (verbose)) &&                        \
    printf(fmt, ##args);

// initrd functions
void initrd_create(int argc, char **argv);
void initrd_read(char *filename, fs_t *fs);
void initrd_write(char *file, fs_t *fs);

#endif
