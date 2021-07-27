//
// Created by Aaron Gill-Braun on 2021-07-12.
//

#ifndef KERNEL_QUEUE_H
#define KERNEL_QUEUE_H

#define LIST_HEAD(type) \
  struct {              \
    type *first;        \
    type *last;         \
  }

#define LIST_ENTRY(type) \
  struct {               \
    type *next;          \
    type *prev;          \
  }

#define SLIST_ENTRY(type) \
  struct {                \
    type *next;           \
  }

// List functions

#define LIST_INIT(head)   \
  {                       \
    (head)->first = NULL; \
    (head)->last = NULL;  \
  }

/* Adds an element to the end of the list */
#define LIST_ADD(head, el, name)        \
  {                                     \
    if ((head)->first == NULL) {        \
      (head)->first = (el);             \
      (head)->last = (el);              \
      (el)->name.next = NULL;         \
      (el)->name.prev = NULL;         \
    } else {                            \
      (head)->last->name.next = (el); \
      (el)->name.next = NULL;         \
      (el)->name.prev = (head)->last; \
      (head)->last = (el);              \
    }                                   \
  }

/* Adds an element to the start of the list */
#define LIST_ADD_FRONT(head, el, name)   \
  {                                      \
    if ((head)->first == NULL) {         \
      (head)->first = (el);              \
      (head)->last = (el);               \
      (el)->name.next = NULL;          \
      (el)->name.prev = NULL;          \
    } else {                             \
      (head)->first->name.prev = (el); \
      (el)->name.next = (head)->first; \
      (el)->name.prev = NULL;          \
      (head)->first = (el);              \
    }                                    \
  }

/* Removes an element from the list */
#define LIST_REMOVE(head, el, name)                       \
  {                                                       \
    if ((el) == (head)->first) {                          \
      if ((el) == (head)->last) {                         \
        (head)->first = NULL;                             \
        (head)->last = NULL;                              \
      } else {                                            \
        (el)->name.next->name.prev = NULL;            \
        (head)->first = (el)->name.next;                \
        (el)->name.next = NULL;                         \
      }                                                   \
    } else if ((el) == (head)->last) {                    \
      (el)->name.prev->name.next = NULL;              \
      (head)->last = (el)->name.prev;                   \
      (el)->name.prev = NULL;                           \
    } else {                                              \
      (el)->name.next->name.prev = (el)->name.prev; \
      (el)->name.prev->name.next = (el)->name.next; \
      (el)->name.next = NULL;                           \
      (el)->name.prev = NULL;                           \
    }                                                     \
  }

/* Adds a list to the end of another */
#define LIST_CONCAT(head, start, end, name)               \
  {                                                       \
    if ((head)->first == NULL) {                          \
      (head)->first = (start);                            \
      (head)->last = (end);                               \
    } else {                                              \
      (head)->last->name.next = (start);                  \
      (start)->name.prev = (head)->last;                  \
      (head)->last = (end);                               \
    }                                                     \
  }

// Raw list functions

#define RLIST_ADD(el1, el2, name) \
  {                               \
    (el1)->name.next = (el2);     \
    (el2)->name.prev = (el1);     \
  }

/*
 * Adds an element to the front of a raw list (no head).
 *  - `ptr` is a pointer to a pointer to the element type
 **/
#define RLIST_ADD_FRONT(ptr, el, name) \
  {                                    \
    if (*(ptr) != NULL) {              \
      (el)->name.next = *(ptr);      \
      (*(ptr))->name.prev = (el);    \
    }                                  \
    *(ptr) = (el);                     \
  }

/*
 * Removes an element from a raw list (no head).
 *  - `ptr` is a pointer to a pointer to the first element
 **/
#define RLIST_REMOVE(ptr, el, name)                   \
  {                                                   \
    if (*(ptr) != el) {                               \
      *(ptr) = NULL;                                  \
    } else {                                          \
      if ((el)->name.prev) {                          \
        (el)->name.prev->name.next = (el)->name.next; \
      }                                               \
      if ((el)->name.next) {                          \
        (el)->name.next->name.prev = (el)->name.prev; \
      }                                               \
    }                                                 \
  }


// List helpers

#define LIST_FOREACH(var, head, name) \
  for ((var) = ((head)->first); (var); (var) = ((var)->name.next))

#define RLIST_FOREACH(var, el, name) \
  for ((var) = (el); (var); (var) = ((var)->name.next))

#define LIST_FIND(var, head, name, cond) \
  {                                      \
    LIST_FOREACH(var, head, name) {      \
      if (cond)                          \
        break;                           \
    }                                    \
  }

#define RLIST_GET_LAST(el, name)              \
  {                                           \
    while ((el) && (el)->name.next != NULL) { \
      (el) = (el)->name.next;                 \
    }                                         \
  }

// List accessors

#define LIST_EMPTY(head) ((head)->first == NULL)
#define LIST_FIRST(head) ((head)->first)
#define LIST_LAST(head) ((head)->last)
#define LIST_NEXT(el, name) ((el)->name.next)
#define LIST_PREV(el, name) ((el)->name.prev)


#endif
