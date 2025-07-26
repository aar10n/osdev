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

#define SLIST_ENTRY(type) type *
#define RLIST_HEAD(type) type *

// List functions

#define LIST_HEAD_INITR { NULL, NULL }

#define LIST_INIT(head)   \
  ({                      \
    typeof(head) __head = (head); \
    (__head)->first = NULL; \
    (__head)->last = NULL;  \
    (__head);               \
  })

#define LIST_ENTRY_INIT(entry) \
  ({                           \
    (entry)->next = NULL;      \
    (entry)->prev = NULL;      \
    (entry);                   \
  })

/* Adds an element to the end of the list */
#define LIST_ADD(head, el, name)        \
  ({                                    \
    typeof(el) __el = (el);             \
    if ((head)->first == NULL) {        \
      (head)->first = (__el);           \
      (head)->last = (__el);            \
      (__el)->name.next = NULL;         \
      (__el)->name.prev = NULL;         \
    } else {                            \
      (head)->last->name.next = (__el); \
      (__el)->name.next = NULL;         \
      (__el)->name.prev = (head)->last; \
      (head)->last = (__el);            \
    }                                   \
    (__el);                             \
  })

/* Adds an element to the start of the list */
#define LIST_ADD_FRONT(head, el, name)  \
  ({                                    \
    typeof(el) __el = (el);             \
    if ((head)->first == NULL) {        \
      (head)->first = (__el);           \
      (head)->last = (__el);            \
      (__el)->name.next = NULL;         \
      (__el)->name.prev = NULL;         \
    } else {                            \
      (head)->first->name.prev = (__el);\
      (__el)->name.next = (head)->first;\
      (__el)->name.prev = NULL;         \
      (head)->first = (__el);           \
    }                                   \
    (__el); \
  })

/* inserts an element after another */
#define LIST_INSERT(head, el, name, after)                \
  ({                                                      \
    typeof(el) __el = (el);                               \
    if ((after) == (head)->last) {                        \
      (head)->last = (__el);                              \
    } else {                                              \
      (after)->name.next->name.prev = (__el);             \
    }                                                     \
    (__el)->name.next = (after)->name.next;               \
    (__el)->name.prev = (after);                          \
    (after)->name.next = (__el);                          \
    (__el);                                               \
  })

/* inserts an element into the correct position of a key-ordered list */
#define LIST_INSERT_ORDERED_BY(head, el, name, order_key) \
  ({                                                      \
    typeof(el) __p = (head)->first;                       \
    typeof(el) ___el = (el);                              \
    while (__p && __p->order_key < ___el->order_key) {    \
      __p = __p->name.next;                               \
    }                                                     \
    if (__p) {                                            \
      LIST_INSERT(head, ___el, name, __p->name.prev);     \
    } else {                                              \
      LIST_ADD(head, ___el, name);                        \
    }                                                     \
    (el);                                                 \
  })

/* Removes an element from the list */
#define LIST_REMOVE(head, el, name)                       \
  ({                                                      \
    typeof(el) __el = (el);                               \
    if (__el == (head)->first) {                          \
      if (__el == (head)->last) {                         \
        (head)->first = NULL;                             \
        (head)->last = NULL;                              \
      } else {                                            \
        __el->name.next->name.prev = NULL;                \
        (head)->first = __el->name.next;                  \
        __el->name.next = NULL;                           \
      }                                                   \
    } else if (__el == (head)->last) {                    \
      __el->name.prev->name.next = NULL;                  \
      (head)->last = __el->name.prev;                     \
      __el->name.prev = NULL;                             \
    } else {                                              \
      __el->name.next->name.prev = __el->name.prev;       \
      __el->name.prev->name.next = __el->name.next;       \
      __el->name.next = NULL;                             \
      __el->name.prev = NULL;                             \
    }                                                     \
    __el;                                                 \
  })

#define LIST_SORT_BY(head, name, order_key)                             \
  ({                                                                    \
    if ((head)->first == NULL || (head)->first == (head)->last) {      \
      (head);                                                           \
    }                                                                   \
    typeof((head)->first) __sorted = NULL;                              \
    typeof((head)->first) __cur = (head)->first;                        \
    while (__cur) {                                                     \
      typeof((head)->first) __next = __cur->name.next;                 \
      __cur->name.prev = __cur->name.next = NULL;                      \
      if (__sorted == NULL) {                                           \
        __sorted = __cur;                                               \
        __sorted->name.prev = __sorted->name.next = NULL;              \
      } else if (__cur->order_key < __sorted->order_key) {             \
        __cur->name.next = __sorted;                                    \
        __sorted->name.prev = __cur;                                    \
        __sorted = __cur;                                               \
      } else {                                                          \
        typeof((head)->first) __scan = __sorted;                        \
        while (__scan->name.next &&                                     \
               __scan->name.next->order_key < __cur->order_key) {      \
          __scan = __scan->name.next;                                   \
        }                                                               \
        __cur->name.next = __scan->name.next;                          \
        if (__scan->name.next)                                          \
          __scan->name.next->name.prev = __cur;                        \
        __scan->name.next = __cur;                                     \
        __cur->name.prev = __scan;                                     \
      }                                                                 \
      __cur = __next;                                                   \
    }                                                                   \
    (head)->first = __sorted;                                           \
    (head)->last = __sorted;                                            \
    while ((head)->last && (head)->last->name.next)                    \
      (head)->last = (head)->last->name.next;                          \
    (head);                                                             \
  })

/* Removes the first element from the list */
#define LIST_REMOVE_FIRST(head, name) \
  ({                                  \
    typeof((head)->first) __el = (head)->first; \
    if (__el) {                       \
      if (__el == (head)->last) {    \
        (head)->first = NULL;         \
        (head)->last = NULL;          \
      } else {                        \
        (head)->first = __el->name.next; \
        (head)->first->name.prev = NULL; \
      }                               \
      __el->name.next = NULL;         \
      __el->name.prev = NULL;         \
    }                                 \
    __el;                             \
  })

/* Adds a list to the end of another */
#define LIST_CONCAT(head, start, end, name)               \
  ({                                                      \
    if ((head)->first == NULL) {                          \
      (head)->first = (start);                            \
      (head)->last = (end);                               \
    } else if ((start) && (end)) {                        \
      (head)->last->name.next = (start);                  \
      (start)->name.prev = (head)->last;                  \
      (head)->last = (end);                               \
    }                                                     \
    (head);                                               \
  })

// Single linked list

#define SLIST_INITIALIZER { NULL }

/* Adds an element to the end of the single list */
#define SLIST_ADD(head, el, name)       \
  {                                     \
    typeof((head)->first) __el = (el);  \
    if ((head)->first == NULL) {        \
      (head)->first = (__el);           \
      (head)->last = (__el);            \
      (__el)->name = NULL;              \
    } else {                            \
      (head)->last->name = (__el);      \
      (__el)->name = NULL;              \
      (head)->last = (__el);            \
    }                                   \
  }

/* Adds an element to the start of the list */
#define SLIST_ADD_FRONT(head, el, name)  \
  {                                      \
    typeof((head)->first) __el = (el);   \
    if ((head)->first == NULL) {         \
      (head)->first = (__el);            \
      (head)->last = (__el);             \
      (__el)->name = NULL;               \
    } else {                             \
      (__el)->name = (head)->first;      \
      (head)->first = (__el);            \
    }                                    \
  }

/* Concatenates two single-lists */
#define SLIST_ADD_EL(end, other, name)   \
  {                                      \
    (end)->name = other;                 \
  }

#define SLIST_ADD_SLIST(head, other_start, other_end, name) \
  { \
    if ((head)->first == NULL) {                            \
      (head)->first = (other_start);                        \
      (head)->last = (other_end);                           \
      (other_end)->name = NULL;                             \
    } else {                                                \
      (head)->last->name = (other_start);                   \
      (head)->last = (other_end);                           \
      (other_end)->name = NULL;                             \
    }                                                       \
  }


#define SLIST_GET_LAST(el, name)        \
  ({                                    \
    typeof(el) __p = el;                \
    while (__p && __p->name != NULL) {  \
      __p = __p->name;                  \
    }                                   \
    __p;                                \
  })

#define SLIST_REMOVE(head, el, name) \
  {                                  \
    typeof((head)->first) __el = (head)->first; \
    if (__el == (el)) {              \
      (head)->first = __el->name;    \
    } else {                         \
      while (__el && __el->name != (el)) { \
        __el = __el->name;           \
      }                               \
      if (__el) {                    \
        __el->name = __el->name->name; \
      }                               \
    }                                 \
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
    typeof(el) __el = (el);            \
    if (*(ptr) != NULL) {              \
      __el->name.next = *(ptr);      \
      (*(ptr))->name.prev = __el;    \
    }                                  \
    *(ptr) = __el;                     \
  }

/*
 * Removes an element from a raw list (no head).
 *  - `ptr` is a pointer to a pointer to the first element
 **/
#define RLIST_REMOVE(ptr, el, name)                   \
  {                                                   \
    typeof(el) __el = (el);                           \
    if (*(ptr) == __el) {                             \
      *(ptr) = __el->name.next;                       \
    } else {                                          \
      if (__el->name.prev) {                          \
        __el->name.prev->name.next = __el->name.next; \
      }                                               \
      if (__el->name.next) {                          \
        __el->name.next->name.prev = __el->name.prev; \
      }                                               \
    }                                                 \
  }


// List helpers

#define LIST_FOREACH(var, head, name) \
  for ((var) = ((head)->first); (var); (var) = ((var)->name.next))

#define RLIST_FOREACH(var, el, name) \
  for ((var) = (el); (var); (var) = ((var)->name.next))

#define SLIST_FOREACH(var, el, name) \
  for ((var) = (el); (var); (var) = ((var)->name))

// ---------------
// alternate versions where you dont need to predeclare var

#define LIST_FOR_IN(var, head, name) \
  for (typeof((head)->first) (var) = ((head)->first); (var); (var) = ((var)->name.next))

#define RLIST_FOR_IN(var, el, name) \
  for (typeof(el) (var) = (el); (var); (var) = ((var)->name.next))

#define SLIST_FOR_IN(var, el, name) \
  for (typeof(el) (var) = (el); (var); (var) = ((var)->name))

// ---------------
// safe versions where the list may be modified during iteration

#define LIST_FOR_IN_SAFE(var, head, name) \
  for (typeof((head)->first) (var) = ((head)->first), _next = (var) ? (var)->name.next : NULL; \
       (var); \
       (var) = _next, _next = (var) ? (var)->name.next : NULL)

#define SLIST_FOR_IN_SAFE(var, el, name) \
  for (typeof(el) (var) = (el), _next = (var) ? (var)->name : NULL; \
       (var); \
       (var) = _next, _next = (var) ? (var)->name : NULL)



#define LIST_FIND(var, head, name, cond) \
  ({                                     \
    typeof(LIST_FIRST(head)) var = {0};  \
    LIST_FOREACH(var, head, name) {      \
      if (cond)                          \
        break;                           \
    }                                    \
    var;\
  })

#define RLIST_FIND(var, el, name, cond)  \
  ({                                     \
    typeof(el) var = {0};                \
    RLIST_FOREACH(var, el, name) {       \
      if (cond)                          \
        break;                           \
    }                                    \
    var;\
  })

#define SLIST_FIND(var, el, name, cond) \
  ({                                    \
    typeof(el) var = {0};               \
    SLIST_FOREACH(var, el, name) {      \
      if (cond)                         \
        break;                          \
    }                                   \
    var;                                \
  })

#define RLIST_GET_LAST(el, name)        \
  ({                                    \
    typeof(el) var = {0};               \
    RLIST_FOREACH(var, el, name) {      \
      if (var->name.next == NULL)       \
        break;                          \
    }                                   \
    var;                                \
  })

// List accessors

#define LIST_EMPTY(head) ((head)->first == NULL)
#define LIST_FIRST(head) ((head)->first)
#define LIST_LAST(head) ((head)->last)
#define LIST_NEXT(el, name) ((el)->name.next)
#define LIST_PREV(el, name) ((el)->name.prev)


#endif
