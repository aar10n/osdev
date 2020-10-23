//
// Created by Aaron Gill-Braun on 2020-10-23.
//

#include "queue.h"

#ifndef _malloc
#include <mm/heap.h>
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif


static inline void enqueue_free(queue_t *queue, queue_item_t *free) {
  free->next = queue->free.list;
  free->data = NULL;
  queue->free.count++;
  queue->free.list = free;
}

static inline queue_item_t *dequeue_free(queue_t *queue) {
  if (queue->free.count == 0) {
    void *chunk = kmalloc(sizeof(queue_item_t) * QUEUE_SIZE);
    uintptr_t ptr = (uintptr_t) chunk;
    queue_item_t *last = NULL;
    for (int i = 0; i < QUEUE_SIZE; i++) {
      queue_item_t *item = (void *) ptr;
      item->next = NULL;
      item->data = NULL;
      if (last) {
        last->next = item;
      }
      ptr += sizeof(queue_item_t);
      last = item;
    }

    queue_item_t *header = kmalloc(sizeof(queue_item_t));
    header->data = chunk;
    header->next = queue->free.headers;

    queue->free.count = QUEUE_SIZE;
    queue->free.headers = header;
    queue->free.list = chunk;
  }

  queue_item_t *free = queue->free.list;
  queue->free.count--;
  queue->free.list = free->next;
  free->next = NULL;
  return free;
}


//

queue_t *create_queue() {
  queue_t *queue = _malloc(sizeof(queue_t));
  queue->count = 0;
  queue->front = NULL;
  queue->back = NULL;
  spin_init(&queue->lock);

  void *chunk = _malloc(sizeof(queue_item_t) * QUEUE_SIZE);
  uintptr_t ptr = (uintptr_t) chunk;
  queue_item_t *last = NULL;
  for (int i = 0; i < QUEUE_SIZE; i++) {
    queue_item_t *item = (void *) ptr;
    item->next = NULL;
    item->data = NULL;
    if (last) {
      last->next = item;
    }
    ptr += sizeof(queue_item_t);
    last = item;
  }

  queue_item_t *header = _malloc(sizeof(queue_item_t));
  header->next = NULL;
  header->data = chunk;
  queue->free.count = QUEUE_SIZE;

  queue->free.headers = header;
  queue->free.list = chunk;
  return queue;
}

void destroy_queue(queue_t *queue) {
  queue_item_t *item = queue->free.headers;
  while (item) {
    queue_item_t *next = item->next;
    _free(item->data);
    _free(item);
    item = next;
  }
  _free(queue);
}

void enqueue(queue_t *queue, void *item) {
  spin_lock(&queue->lock);
  // ----------------
  queue_item_t *free = dequeue_free(queue);
  free->data = item;
  if (queue->count == 0) {
    queue->front = free;
    queue->back = free;
  } else {
    queue->back->next = free;
    queue->back = free;
  }
  queue->count++;
  // ----------------
  spin_unlock(&queue->lock);
}

void enqueue_front(queue_t *queue, void *item) {
  spin_lock(&queue->lock);
  // ----------------
  queue_item_t *free = dequeue_free(queue);
  free->data = item;
  if (queue->count == 0) {
    queue->front = free;
    queue->back = free;
  } else {
    free->next = queue->front;
    queue->front = free;
  }
  queue->count++;
  // ----------------
  spin_unlock(&queue->lock);
}

void *dequeue(queue_t *queue) {
  if (queue->count == 0) {
    return NULL;
  }

  spin_lock(&queue->lock);
  // ----------------
  queue_item_t *item = queue->front;
  if (queue->count == 1) {
    queue->front = NULL;
    queue->back = NULL;
  } else {
    queue->front = item->next;
  }
  queue->count--;

  void *data = item->data;
  enqueue_free(queue, item);
  // ----------------
  spin_unlock(&queue->lock);
  return data;
}
