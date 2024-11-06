//
// Created by Aaron Gill-Braun on 2023-12-22.
//

#include <kernel/lock.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

#define MAX_CLAIMS 8

/*
 * A lock claim is a record of a lock held by an owner.
 */
struct lock_claim {
  struct lock_object *lock; // the owned lock
  uintptr_t how;            // how the lock was acquired
  const char *file;         // file where the lock was acquired
  int line;                 // line where the lock was acquired
};

/*
 * A list of lock_claims.
 * It is used to hold a number of lock claims and avoid allocations in the locking
 * path which results in a new claim being written. We only need to allocate when
 * the per-node list is full.
 */
struct lock_claim_list {
  struct lock_claim claims[MAX_CLAIMS];// list of claims
  int nclaims;                  // number of claims
  struct lock_claim_list *next; // next list
};

// MARK: lock claims

struct lock_claim_list *lock_claim_list_alloc() {
  struct lock_claim_list *list = kmallocz(sizeof(struct lock_claim_list));
  list->nclaims = 0;
  list->next = NULL;
  return list;
}

void lock_claim_list_free(struct lock_claim_list **listp) {
  struct lock_claim_list *list = *listp;
  while (list) {
    struct lock_claim_list *next = list->next;
    kfree(list);
    list = next;
  }
  *listp = NULL;
}

void lock_claim_list_add(struct lock_claim_list *list, struct lock_object *lock, uintptr_t how, const char *file, int line) {
  if (list->nclaims == MAX_CLAIMS) {
    struct lock_claim_list *next = lock_claim_list_alloc();
    list->next = next;
    list = next;
  }

  struct lock_claim *claim = &list->claims[list->nclaims++];
  claim->lock = lock;
  claim->how = how;
  claim->file = file;
  claim->line = line;
}

void lock_claim_list_remove(struct lock_claim_list *list, struct lock_object *lock) {
  // get the last list in the chain
  while (list->nclaims == MAX_CLAIMS && list->next) {
    list = list->next;
  }

  // scan in reverse order to find the most recent claim
  for (int i = list->nclaims - 1; i >= 0; i--) {
    if (list->claims[i].lock == lock) {
      list->claims[i].lock = NULL;
      list->claims[i].how = 0;
      list->claims[i].file = NULL;
      list->claims[i].line = 0;
      list->nclaims--;
      return;
    }
  }

  // if we get here then the lock was not found
  panic("lock_claim_list_remove() on unowned lock");
}

// MARK: spin delay

int spin_delay_wait(struct spin_delay *delay) {
  if (delay->waits >= delay->max_waits) {
    return 0;
  }

  register uint64_t count asm ("r15") = delay->delay_count;
  while (count--) {
    cpu_pause();
  }

  delay->waits++;
  return 1;
}

//

static void percpu_early_init_claim_list() {
  // this per-cpu lock lock_claim_list tracks spin lock claims
  struct lock_claim_list *list = lock_claim_list_alloc();
  PERCPU_AREA->spin_claims = list;
}
PERCPU_EARLY_INIT(percpu_early_init_claim_list);
