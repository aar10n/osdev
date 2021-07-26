//
// Created by Aaron Gill-Braun on 2021-07-18.
//

#ifndef FS_CHRDEV_H
#define FS_CHRDEV_H

#include <base.h>
#include <mutex.h>

typedef struct file_ops file_ops_t;


typedef struct chrdev {
  file_ops_t *ops;
} chrdev_t;

chrdev_t *chrdev_init(file_ops_t *ops);

#endif
