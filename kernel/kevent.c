//
// Created by Aaron Gill-Braun on 2025-07-20.
//

#include <kernel/kevent.h>
#include <kernel/mm.h>
#include <kernel/time.h>
#include <kernel/tqueue.h>
#include <kernel/proc.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
//#define DPRINTF(fmt, ...) kprintf("kevent: " fmt, ##__VA_ARGS__)
#define DPRINTF(fmt, ...)
#define EPRINTF(fmt, ...) kprintf("kevent: %s: " fmt, __func__, ##__VA_ARGS__)

#define KQUEUE_HASH_SIZE 256
#define KQUEUE_HASH(ident, filter) (((ident) ^ (filter)) & (KQUEUE_HASH_SIZE - 1))


struct filter_ops *filter_ops[NEVFILT];

#define FILTER_NAME(filt) [(-(filt)) - 1] = #filt
const char *filter_names[] = {
  FILTER_NAME(EVFILT_READ),
  FILTER_NAME(EVFILT_WRITE),
  FILTER_NAME(EVFILT_AIO),
  FILTER_NAME(EVFILT_VNODE),
  FILTER_NAME(EVFILT_PROC),
  FILTER_NAME(EVFILT_SIGNAL),
  FILTER_NAME(EVFILT_TIMER),
  FILTER_NAME(EVFILT_PROCDESC),
  FILTER_NAME(EVFILT_FS),
  FILTER_NAME(EVFILT_LIO),
  FILTER_NAME(EVFILT_USER),
  FILTER_NAME(EVFILT_SENDFILE),
  FILTER_NAME(EVFILT_EMPTY),
};
#undef FILTER_NAME

#define filter_index(filter) ((-(filter)) - 1)
#define filter_index_safe(filter) ({ \
  ASSERT(((filter) >= -NEVFILT && (filter) < 0) && "invalid filter"); \
  (-(filter)) - 1; \
})


void register_filter_ops(int16_t filter, struct filter_ops *ops) {
  ASSERT(ops != NULL);

  int index = filter_index_safe(filter);
  if (filter_ops[index] != NULL) {
    EPRINTF("filter_ops already registered for filter %s\n", filter_names[index]);
    panic("register_filter_ops: filter already registered for %s", filter_names[index]);
  }
  filter_ops[index] = ops;
  DPRINTF("registered filter ops for %s\n", filter_names[index]);
}

const char *evfilt_to_string(int16_t filter) {
  int index = filter_index(filter);
  if (index < 0 || index >= NEVFILT) {
    EPRINTF("evfilt_to_string: invalid filter %d\n", filter);
    return "INVALID";
  }

  return filter_names[index];
}

struct filter_ops *get_filter_ops(int16_t filter) {
  int index = filter_index_safe(filter);
  return filter_ops[index];
}

//
// MARK: knote API
//

knote_t *knote_alloc() {
  knote_t *kn = kmallocz(sizeof(knote_t));
  if (kn == NULL) {
    panic("knote_alloc: failed to allocate knote\n");
  }
  return kn;
}

void knote_free(knote_t **knp) {
  knote_t *kn = moveptr(*knp);
  if (kn == NULL) {
    return;
  }

  ASSERT(kn->filt_ops != NULL);
  ASSERT(kn->filt_ops_data == NULL);
  ASSERT(kn->kq == NULL);
  ASSERT(kn->knlist == NULL);
  ASSERT(kn->fde == NULL);
  kfree(kn);
}

void knote_activate(knote_t *kn) {
  kqueue_t *kq = kn->kq;
  ASSERT(kq != NULL);
  ASSERT(!(kn->flags & KNF_ACTIVE));
  kn->flags |= KNF_ACTIVE;
  knlist_add(&kq->active, kn);

  // wake up any threads waiting on this kqueue
  struct waitqueue *wq = waitq_lookup(kq);
  if (wq != NULL) {
    waitq_broadcast(wq);
  }
}

void knote_add_list(knote_t *kn, struct knlist *knl) {
  ASSERT(kn->knlist == NULL);
  knlist_add(knl, kn);
  kn->knlist = knl;
}

void knote_remove_list(knote_t *kn) {
  ASSERT(kn->knlist != NULL);
  kqueue_t *kq = kn->kq;
  if (kn->flags & KNF_ACTIVE) {
    knlist_remove(&kq->active, kn);
    kn->flags &= ~KNF_ACTIVE;
  } else {
    knlist_remove(kn->knlist, kn);
  }
  kn->knlist = NULL;
}

//
// MARK: knlist API
//

void knlist_init(struct knlist *knl, struct lock_object *lo) {
  // associated lock must be initialized before the knlist
  ASSERT(lo->flags & LO_INITIALIZED);

  struct lock_class *lc = lock_class_lookup(LO_LOCK_CLASS(lo));
  knl->lock_object = lo;
  knl->lock_class = lc;
  knl->count = 0;
  LIST_INIT(&knl->knotes);
}

void knlist_destroy(struct knlist *knl) {
  ASSERT(LIST_EMPTY(&knl->knotes));
  knl->count = 0;
}

void knlist_add(struct knlist *knl, knote_t *kn) {
  ASSERT(kn->filt_ops != NULL);

  struct lock_class *lc = knl->lock_class;
  struct lock_object *lo = knl->lock_object;
  bool locked = false;
  if (lockclass_owner(lc, lo) != curthread) {
    lockclass_lock(lc, lo, LC_EXCL);
    locked = true;
  }

  // check for duplicate knote with same ident/filter
  knote_t *existing_kn = LIST_FIND(_kn, &knl->knotes, klist, ({
    _kn->event.ident == kn->event.ident && _kn->event.filter == kn->event.filter;
  }));
  ASSERT(existing_kn == NULL);
  
  // add knote to this list
  LIST_ADD_FRONT(&knl->knotes, kn, klist);
  knl->count++;

  if (locked) lockclass_unlock(lc, lo);
}

void knlist_remove(struct knlist *knl, knote_t *kn) {
  ASSERT(knl != NULL);
  ASSERT(kn->filt_ops != NULL);

  struct lock_class *lc = knl->lock_class;
  struct lock_object *lo = knl->lock_object;
  bool locked = false;
  if (lockclass_owner(lc, lo) != curthread) {
    lockclass_lock(lc, lo, LC_EXCL);
    locked = true;
  }

  LIST_REMOVE(&knl->knotes, kn, klist);
  knl->count--;

  if (locked) lockclass_unlock(lc, lo);
}

int knlist_activate_notes(struct knlist *knl, long hint) {
  int activated = 0;

  struct lock_class *lc = knl->lock_class;
  struct lock_object *lo = knl->lock_object;
  bool locked = false;
  if (lockclass_owner(lc, lo) != curthread) {
    lockclass_lock(lc, lo, LC_EXCL);
    locked = true;
  }

  // we need to iterate safely since we'll be removing items
  LIST_FOR_IN_SAFE(kn, &knl->knotes, klist) {
    if (kn->filt_ops->f_event(kn, hint)) {
      LIST_REMOVE(&knl->knotes, kn, klist);
      knl->count--;

      // now activate it (which will add to active list)
      knote_activate(kn);
      activated++;
    }
  }

  if (locked) lockclass_unlock(lc, lo);
  return activated;
}

//
// MARK: kqueue API
//

static knote_t *kqueue_find_knote(kqueue_t *kq, uintptr_t ident, int16_t filter) {
  uint32_t hash = KQUEUE_HASH(ident, filter);
  SLIST_FOR_IN(kn, LIST_FIRST(&kq->knhash[hash]), hlist) {
    if (kn->event.ident == ident && kn->event.filter == filter) {
      return kn;
    }
  }
  return NULL;
}

static void kqueue_hash_add(kqueue_t *kq, knote_t *kn) {
  uint32_t hash = KQUEUE_HASH(kn->event.ident, kn->event.filter);
  SLIST_ADD_FRONT(&kq->knhash[hash], kn, hlist);
}

static void kqueue_hash_remove(kqueue_t *kq, knote_t *kn) {
  uint32_t hash = KQUEUE_HASH(kn->event.ident, kn->event.filter);
  SLIST_REMOVE(&kq->knhash[hash], kn, hlist);
}

static int kqueue_attach_knote(kqueue_t *kq, knote_t *kn) {
  DPRINTF("kqueue_attach_knote: attaching knote for ident %p, filter %s\n",
          kn->event.ident, evfilt_to_string(kn->event.filter));
  int res;
  ASSERT(kn->filt_ops->f_attach != NULL);
  res = kn->filt_ops->f_attach(kn);
  if (res < 0) {
    return res;
  }
  ASSERT(kn->filt_ops_data != NULL);
  ASSERT(kn->knlist != NULL);

  kqueue_hash_add(kq, kn);
  kn->kq = kq;
  return 0;
}

static void kqueue_detach_knote(kqueue_t *kq, knote_t *kn) {
  DPRINTF("kqueue_detach_knote: detaching knote for ident %p, filter %s\n",
          kn->event.ident, evfilt_to_string(kn->event.filter));
  if (kn->filt_ops && kn->filt_ops->f_detach) {
    kn->filt_ops->f_detach(kn);
  } else {
    knote_remove_list(kn);
  }
  ASSERT(kn->knlist == NULL);

  kqueue_hash_remove(kq, kn);
  kn->kq = NULL;
}

//

kqueue_t *kqueue_alloc() {
  size_t total_size = sizeof(kqueue_t) + (KQUEUE_HASH_SIZE * sizeof(LIST_HEAD(struct knote)));
  kqueue_t *kq = kmallocz(total_size);
  if (kq == NULL) {
    return NULL;
  }
  
  kq->state = 0;
  mtx_init(&kq->lock, 0, "kqueue");
  knlist_init(&kq->active, &kq->lock.lo);
  for (int i = 0; i < KQUEUE_HASH_SIZE; i++) {
    LIST_INIT(&kq->knhash[i]);
  }

  return kq;
}

void kqueue_free(kqueue_t **kqp) {
  kqueue_t *kq = moveptr(*kqp);
  if (kq == NULL) {
    return;
  }

  mtx_assert(&kq->lock, MA_UNLOCKED);

  // ensure all knotes are removed
  ASSERT(kq->active.count == 0);
  for (int i = 0; i < KQUEUE_HASH_SIZE; i++) {
    ASSERT(LIST_EMPTY(&kq->knhash[i]));
  }

  knlist_destroy(&kq->active);
  mtx_destroy(&kq->lock);
  kfree(kq);
}

void kqueue_drain(kqueue_t *kq) {
  mtx_lock(&kq->lock);

  // drain all knotes
  for (int i = 0; i < KQUEUE_HASH_SIZE; i++) {
    SLIST_FOR_IN_SAFE(kn, LIST_FIRST(&kq->knhash[i]), hlist) {
      kqueue_detach_knote(kq, kn);
      knote_free(&kn);
    }
  }

  knlist_destroy(&kq->active);
  knlist_init(&kq->active, &kq->lock.lo);

  mtx_unlock(&kq->lock);
}

//

static int kqueue_register(kqueue_t *kq, struct kevent *kev) {
  knote_t *kn;
  int res;

  mtx_lock(&kq->lock);

  // find existing knote if any
  kn = kqueue_find_knote(kq, kev->ident, kev->filter);

  // handle deletion
  if (kev->flags & EV_DELETE) {
    if (kn == NULL) {
      EPRINTF("knote not found for ident %p, filter %s\n", kev->ident, evfilt_to_string(kev->filter));
      goto_res(ret, -ENOENT);
    }

    DPRINTF("kqueue_register: deleting knote for ident %p, filter %s\n", kev->ident, evfilt_to_string(kev->filter));
    kqueue_detach_knote(kq, kn);
    knote_free(&kn);
    goto_res(ret, 0); // success
  }

  // handle addition
  if (kn == NULL && (kev->flags & EV_ADD)) {
    // validate filter
    int filter_index = filter_index(kev->filter);
    if (filter_index < 0 || filter_index >= NEVFILT || filter_ops[filter_index] == NULL) {
      EPRINTF("invalid filter %d\n", kev->filter);
      goto_res(ret, -EINVAL);
    }

    DPRINTF("kqueue_register: adding knote for ident %p, filter %s\n", kev->ident, evfilt_to_string(kev->filter));

    // create new knote
    kn = knote_alloc();
    kn->event = *kev;
    kn->filt_ops = filter_ops[filter_index];
    if ((res = kqueue_attach_knote(kq, kn)) < 0) {
      EPRINTF("kqueue_register: failed to attach knote: {:err}\n", res);
      knote_free(&kn);
      goto_res(ret, res);
    }
  } else if (kn != NULL) {
    // handle update
    DPRINTF("kqueue_register: updating knote for ident %p, filter %s\n", kev->ident, evfilt_to_string(kev->filter));
    if (kev->flags & EV_ENABLE) {
      kn->event.flags &= ~EV_DISABLE;
    }
    if (kev->flags & EV_DISABLE) {
      kn->event.flags |= EV_DISABLE;
    }
    if (kev->flags & EV_CLEAR) {
      kn->event.flags |= EV_CLEAR;
    }
    kn->event.udata = kev->udata;
    kn->event.fflags = kev->fflags;
  }

  if (kn && !(kn->flags & KNF_ACTIVE) && kn->filt_ops->f_event(kn, 0)) {
    DPRINTF("kqueue_wait: knote for ident %p, filter %s is ready\n", kev->ident, evfilt_to_string(kev->filter));
    knlist_activate_notes(kn->knlist, 0);
  }
  res = 0; // success
LABEL(ret);
  mtx_unlock(&kq->lock);
  return res;
}

ssize_t kqueue_wait(kqueue_t *kq, struct kevent *changelist, size_t nchanges,
                    struct kevent *eventlist, size_t nevents, struct timespec *timeout) {
  uint64_t timeout_ns = timeout ? timespec_to_nanos(timeout) : 0;
  DPRINTF("kqueue_wait: kq=%p, nchanges=%zu, nevents=%zu, timeout=%lld\n",
          kq, nchanges, nevents, timeout_ns, timeout != NULL ? timeout_ns : -1);
  ssize_t count = 0;
  int res = 0;
  
  // process changelist if provided
  if (changelist && nchanges > 0) {
    for (size_t i = 0; i < nchanges; i++) {
      res = kqueue_register(kq, &changelist[i]);
      if (res < 0) {
        EPRINTF("kqueue_register failed for ident %p, filter %s: {:err}\n",
                changelist[i].ident, evfilt_to_string(changelist[i].filter), res);
        if (changelist[i].flags & EV_RECEIPT) {
          // with EV_RECEIPT, return errors in the changelist itself
          changelist[i].flags = EV_ERROR;
          changelist[i].data = -res;
          count++;
        }
      }
    }
    
    if (count > 0) {
      return count; // return immediately if EV_RECEIPT events
    }
  }
  
  // if no eventlist provided, just return after registration
  if (!eventlist || nevents == 0) {
    return 0;
  }

LABEL(process_events);
  DPRINTF("kqueue_wait: processing active events\n");
  mtx_lock(&kq->lock);

  // check the active events list
  LIST_FOR_IN_SAFE(kn, &kq->active.knotes, klist) {
    if (count >= nevents) {
      break;
    }
    
    if (kn->event.flags & EV_DISABLE) {
      continue;
    }
    
    // verify event is still ready
    if (kn->filt_ops->f_event(kn, 0)) {
      DPRINTF("kqueue_wait: event ready: ident=%p, filter=%s\n",
              kn->event.ident, evfilt_to_string(kn->event.filter));

      // copy event to the eventlist
      eventlist[count++] = kn->event;
      
      // delete oneshot events
      if (kn->event.flags & EV_ONESHOT) {
        kqueue_detach_knote(kq, kn);
        knote_free(&kn);
      } else {
        // remove from active list
        knlist_remove(&kq->active, kn);
        kn->flags &= ~KNF_ACTIVE;

        // handle clear events
        if (kn->event.flags & EV_CLEAR) {
          kn->event.data = 0;
        }

        // re-add to the object list
        knlist_add(kn->knlist, kn);
      }
    } else {
      // event is no longer ready, remove from active list
      knlist_remove(&kq->active, kn);
      // and re-add to the object list
      knlist_add(kn->knlist, kn);
    }
  }
  
  mtx_unlock(&kq->lock);
  if (count > 0 || (timeout && timespec_is_zero(timeout))) {
    DPRINTF("kqueue_wait: returning %zu events\n", count);
    return count;
  }

  // now we need to wait for events, with a timeout if specified
  // but in both cases we allow signals to interrupt the wait
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_CONDV, kq, curthread->own_waitq);
  if (timeout == NULL) {
    res = waitq_wait_sig(waitq, "kqueue_wait");
  } else {
    res = waitq_wait_sigtimeout(waitq, "kqueue_wait", timeout_ns);
  }

  if (res < 0) {
    EPRINTF("kqueue_wait: waitq_wait_sig failed: {:err}\n", res);
    if (res == -EINTR && count > 0) {
      // even if we were interrupted, we can still return events we collected
      return count;
    }
    return res; // error occurred
  }
  goto process_events; // re-check for events after waking up
}
