//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "heap.h"
#include "mm.h"
#include "page.h"

struct free_pages {
  int num_pages;
  page_t *first;
};

struct free_pages free[MAX_ORDER];

void add_page(page_t *page) {
  int order = log2(page->size / PAGE_SIZE);
  if (free[order].first == NULL) {
    free[order].first = page;
    free[order].num_pages += 1;
    page->next = NULL;
    return;
  }

  page->next = free[order].first;
  free[order].first = page;
  free[order].num_pages += 1;
}

void remove_page(page_t *page) {
  int order = log2(page->size / PAGE_SIZE);
  if (page->next == NULL) {
    free[order].first = NULL;
    free[order].num_pages = 0;
    page->next = NULL;
    return;
  }

  free[order].first = page->next;
  free[order].num_pages -= 1;
}

void split_page(page_t *page) {
  page_t *head = _kmalloc(sizeof(page_t));
  page_t *tail = _kmalloc(sizeof(page_t));


  page->flags = PAGE_SPLIT | PAGE_USED;
  page->head = head;

  head->frame = page->frame;
  head->size = page->size / 2;
  head->flags = (PAGE_HEAD | PAGE_FREE);
  head->tail = tail;

  tail->frame = head->frame ^ head->size;
  tail->size = page->size / 2;
  tail->flags = (PAGE_TAIL | PAGE_FREE);
  tail->parent = page;

  remove_page(page);
  add_page(tail);
  add_page(head);
}

void join_page(page_t *page) {
  // free and reuse head & tail

  page->flags = PAGE_FREE;

  remove_page(page->head);
  remove_page(page->head->tail);
  add_page(page);
}


page_t *get_buddy(page_t *page) {
  if (page->flags & PAGE_HEAD) {
    return page->tail;
  } else if (page->flags & PAGE_TAIL) {
    return page->parent->head;
  }
  return NULL;
}

//
//
//

void mem_prealloc(size_t mem_size) {
  size_t available = next_pow2(mem_size) >> 1;
  mem_size -= available;
  while (available != 0) {
    size_t pages = (available / PAGE_SIZE) / 2;
    for (int i = 0; i < MAX_ORDER; i++) {
      if (pages == 0) {
        free[i].num_pages += 1;
        available = next_pow2(mem_size) >> 1;
        mem_size -= available;
        break;
      } else {
        free[i].num_pages += pages;
        pages = pages / 4;
      }
    }
  }
}

page_t *mem_split(int order) {
  page_t *page;

  if (order > MAX_ORDER - 1) {
    // fatal error
    kprintf("fatal error: out of memory\n");
    return NULL;
  } else if (free[order].first == NULL) {
    page = mem_split(order + 1);
  } else {
    page = free[order - 1].first;
  }

  split_page(free[order].first);
  page->flags |= PAGE_USED;

  remove_page(page);
  return page;
}

//
//
//

void mem_init(uintptr_t base_addr, size_t size) {
  mem_prealloc(size);

  uintptr_t page_frame = base_addr;
  for (int i = 0; i < MAX_ORDER; i++) {
    free[i].first = _kmalloc(sizeof(page_t));
    page_t *page = free[i].first;

    for (int j = 0; j < free[i].num_pages; j++) {
      if (j < free[i].num_pages - 1) {
        page->next = _kmalloc(sizeof(page_t));
      }

      page->flags = PAGE_FREE;
      page->size = (1 << i) * PAGE_SIZE;
      page->frame = page_frame;

      page_frame += (1 << i) * PAGE_SIZE;
      page = page->next;
    }
    page_frame += (1 << i) * PAGE_SIZE;
  }
}

page_t *alloc_pages(int order) {
  if (order > MAX_ORDER - 1) {
    // error
    kprintf("error: invalid allocation order\n");
    return NULL;
  } else if (free[order].first == NULL) {
    // split above memory
  }

  page_t *page = free[order].first;
  page->flags ^= PAGE_FREE;
  page->flags |= PAGE_USED;

  remove_page(page);

  map_page(page);
  return page;
}

page_t *alloc_page() {
  return alloc_pages(0);
}

void free_page(page_t *page) {
  page->flags ^= PAGE_USED;
  page->flags |= PAGE_FREE;

  add_page(page);
}
