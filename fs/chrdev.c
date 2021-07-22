//
// Created by Aaron Gill-Braun on 2021-07-18.
//

#include <chrdev.h>

chrdev_t *chrdev_init(file_ops_t *ops) {
  chrdev_t *chrdev = kmalloc(sizeof(chrdev_t));
  chrdev->ops = ops;
  return chrdev;
}
