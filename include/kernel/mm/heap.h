//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H

#include <mm/mm.h>

#define CHUNK_MIN_SIZE 8
#define CHUNK_MAX_SIZE 8192

#define CHUNK_MAGIC 0xABCD
#define HOLE_MAGIC 0xFACE

// 8 bytes
typedef struct chunk {
  uint16_t magic;   // magic number
  uint8_t size : 7; // chunk size in the form of 2^n
  uint8_t free : 1; // is chunk used
  // when chunk is used
  union {
    uint8_t size : 7; // last chunk size in form of 2^n
    uint8_t free : 1; // is last chunk used
  } last;
  // when chunk is used
  struct chunk *next;     // a pointer to the next used chunk
} chunk_t;

typedef struct heap {
  // page_t *source;       // the source of the heap memory
  uintptr_t start_addr; // the heap base address
  uintptr_t end_addr;   // the heap end address
  size_t size;          // the size of the heap
  chunk_t *last_chunk;  // the last created chunk
  chunk_t *chunks;      // a linked list of used chunks
} heap_t;

void kheap_init();

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t size);

#endif
