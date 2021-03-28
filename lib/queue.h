//
// Created by Aaron Gill-Braun on 2020-10-23.
//

#ifndef LIB_QUEUE_H
#define LIB_QUEUE_H

#include <base.h>
// #include <lock.h>

#define QUEUE_SIZE 128

typedef struct queue_item {
  void *data;
  struct queue_item *next;
} queue_item_t;

typedef struct {
  size_t count;
  queue_item_t *front;
  queue_item_t *back;
  spinlock_t lock;
  struct {
    size_t count;
    queue_item_t *headers;
    queue_item_t *list;
  } free;
} queue_t;

queue_t *create_queue();
void destroy_queue();
void enqueue(queue_t *queue, void *item);
void enqueue_front(queue_t *queue, void *item);
void *dequeue(queue_t *queue);

#endif
