//
// Created by Aaron Gill-Braun on 2025-07-20.
//

#ifndef KERNEL_KEVENT_H
#define KERNEL_KEVENT_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/ref.h>
#include <abi/kevent.h>
#include <abi/poll.h>

struct fd_entry;
struct kqueue;
struct knlist;

/*
 * A kernel note is a filtered kernel event.
 */
typedef struct knote {
  int flags;
  struct kevent event;              // event data
  struct kqueue *kq;                // kqueue this note belongs to (ref)
  struct knlist *knlist;            // object knlist this note originates from
  struct fd_entry *fde;             // file descriptor entry (ref)
  struct filter_ops *filt_ops;      // filter operations
  void *filt_ops_data;              // filter private data
  LIST_ENTRY(struct knote) klist;   // knlist entry
  SLIST_ENTRY(struct knote) hlist;  // kqueue hash list entry
} knote_t;

struct filter_ops {
  int (*f_attach)(knote_t *kn);
  void (*f_detach)(knote_t *kn);
  int (*f_event)(knote_t *kn, long hint);
};

#define KNF_ACTIVE 0x01  // knote is active

/*
 * A kernel note list, protected by an external lock.
 */
struct knlist {
  size_t count;                     // number of knotes in the list
  LIST_HEAD(struct knote) knotes;   // the list of knotes
  struct lock_object *lock_object;  // lock object for this knlist
  struct lock_class *lock_class;    // lock class for this knlist
};

/*
 * A kernel event queue owned by a process.
 */
typedef struct kqueue {
  int state;                        // queue state flags
  struct mtx lock;                  // queue mutex
  struct knlist active;             // active events list
  LIST_HEAD(struct knote) knhash[]; // knote hash table
} kqueue_t;


void register_filter_ops(int16_t filter, struct filter_ops *ops);
const char *evfilt_to_string(int16_t filter);

/* knote API */
knote_t *knote_alloc();
void knote_free(knote_t **knp);
void knote_activate(knote_t *kn);
void knote_add_list(knote_t *kn, struct knlist *knl);
void knote_remove_list(knote_t *kn);

/* knlist API */
void knlist_init(struct knlist *knl, struct lock_object *lo);
void knlist_destroy(struct knlist *knl);
void knlist_add(struct knlist *knl, knote_t *kn);
void knlist_remove(struct knlist *knl, knote_t *kn);
int knlist_activate_notes(struct knlist *knl, long hint);

/* kqueue API */
kqueue_t *kqueue_alloc();
void kqueue_free(kqueue_t **kqp);
void kqueue_drain(kqueue_t *kq);

ssize_t kqueue_wait(kqueue_t *kq, struct kevent *changelist, size_t nchanges,
                    struct kevent *eventlist, size_t nevents, struct timespec *timeout);

#endif
