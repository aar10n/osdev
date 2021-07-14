//
// Created by Aaron Gill-Braun on 2021-07-12.
//

#ifndef KERNEL_QUEUE_H
#define KERNEL_QUEUE_H

#define LIST_HEAD(type) \
  struct name {         \
    type *first;        \
    type *last;         \
  }

#define LIST_ENTRY(type) \
  struct {               \
    type *next;          \
    type *prev;          \
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
      (el)->(name).next = NULL;         \
      (el)->(name).prev = NULL;         \
    } else {                            \
      (head)->last->(name).next = (el); \
      (el)->(name).next = NULL;         \
      (el)->(name).prev = (head)->last; \
      (head)->last = (el);              \
    }                                   \
  }

/* Adds an element to the start of the list */
#define LIST_ADD_FRONT(head, el, name)   \
  {                                      \
    if ((head)->first == NULL) {         \
      (head)->first = (el);              \
      (head)->last = (el);               \
      (el)->(name).next = NULL;          \
      (el)->(name).prev = NULL;          \
    } else {                             \
      (head)->first->(name).prev = (el); \
      (el)->(name).next = (head)->first; \
      (el)->(name).prev = NULL;          \
      (head)->first = (el);              \
    }                                    \
  }

/* Adds an element to the start of the list */
#define LIST_ADD_FRONT(head, el, name)   \
  {                                      \
    if ((head)->first == NULL) {         \
      (head)->first = (el);              \
      (head)->last = (el);               \
      (el)->(name).next = NULL;          \
      (el)->(name).prev = NULL;          \
    } else {                             \
      (head)->first->(name).prev = (el); \
      (el)->(name).next = (head)->first; \
      (el)->(name).prev = NULL;          \
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
        (el)->(name).next->(name).prev = NULL;            \
        (head)->first = (el)->(name).next;                \
        (el)->(name).next = NULL;                         \
      }                                                   \
    } else if ((el) == (head)->last) {                    \
      (el)->(name).prev->(name).next = NULL;              \
      (head)->last = (el)->(name).prev;                   \
      (el)->(name).prev = NULL;                           \
    } else {                                              \
      (el)->(name).next->(name).prev = (el)->(name).prev; \
      (el)->(name).prev->(name).next = (el)->(name).next; \
      (el)->(name).next = NULL;                           \
      (el)->(name).prev = NULL;                           \
    }                                                     \
  }

// List helpers

#define LIST_FOREACH(var, head, name) \
  for ((var) = ((head)->first); (var); (var) = ((var)->(name).next))

#define LIST_FIND(var, head, name, cond) \
  {                                      \
    LIST_FOREACH(var, head, name) {      \
      if (cond)                          \
        break;                           \
    }                                    \
  }

// List accessors

#define LIST_EMPTY(head) ((head)->first == NULL)
#define LIST_FIRST(head) ((head)->first)
#define LIST_LAST(head) ((head)->last)
#define LIST_NEXT(el, name) ((el)->(name).next)
#define LIST_PREV(el, name) ((el)->(name).prev)


#endif
