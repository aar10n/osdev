//
// Created by Aaron Gill-Braun on 2025-04-25.
//

#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/proc.h>
#include <kernel/irq.h>
#include <kernel/sched.h>
#include <kernel/mm.h>

#include <kernel/mm/pool.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <rb_tree_v2.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG alarm
#include <kernel/log.h>
#define EPRINTF(x, ...) kprintf("alarm: %s: " x, __func__, ##__VA_ARGS__)

#define HANDLER_FN(fn) ((void (*)(alarm_t *, void *, void *, void *))(fn))

static pool_t *alarm_pool;

static void alarm_pool_init() {
  alarm_pool = pool_create("alarm", pool_sizes(sizeof(alarm_t)), 0);
}
STATIC_INIT(alarm_pool_init);

static LIST_HEAD(alarm_source_t) alarm_sources;
static alarm_source_t *tickless_source = NULL;
static alarm_source_t *tick_source = NULL;

static uint64_t last_tick = 0; // last tick time in nanoseconds
static uint64_t next_tickless_expiry = 0; // next tickless expiry in nanoseconds

// pending alarms sorted by expiry time
static rb_tree_v2_t pending_alarms;
// alarm id lookup tree
static rb_tree_v2_t alarm_ids;
// spinlock for the alarm trees
static mtx_t alarm_lock;
// alarm id atomic "allocator"
static id_t next_alarm_id = 1; // id==0 is invalid

static int expiry_cmp(const rb_node_v2_t *a, const rb_node_v2_t *b) {
  alarm_t *aa = container_of(a, alarm_t, expiry_node);
  alarm_t *bb = container_of(b, alarm_t, expiry_node);
  if (aa->expires_ns < bb->expires_ns) return -1;
  if (aa->expires_ns > bb->expires_ns) return 1;
  return 0;
}

static int expiry_key_cmp(uint64_t key, const rb_node_v2_t *b) {
  alarm_t *bb = container_of(b, alarm_t, expiry_node);
  if (key < bb->expires_ns) return -1;
  if (key > bb->expires_ns) return 1;
  return 0;
}

static int id_cmp(const rb_node_v2_t *a, const rb_node_v2_t *b) {
  alarm_t *aa = container_of(a, alarm_t, id_node);
  alarm_t *bb = container_of(b, alarm_t, id_node);
  if (aa->id < bb->id) return -1;
  if (aa->id > bb->id) return 1;
  return 0;
}

static int id_key_cmp(uint64_t key, const rb_node_v2_t *b) {
  alarm_t *bb = container_of(b, alarm_t, id_node);
  if (key < (uint64_t)bb->id) return -1;
  if (key > (uint64_t)bb->id) return 1;
  return 0;
}

static inline alarm_t *pending_min(void) {
  rb_node_v2_t *n = rb_tree_v2_first(&pending_alarms);
  return n ? container_of(n, alarm_t, expiry_node) : NULL;
}

static inline alarm_t *alarm_find_by_id(id_t id) {
  rb_node_v2_t *n = rb_tree_v2_find(&alarm_ids, (uint64_t)id);
  return n ? container_of(n, alarm_t, id_node) : NULL;
}


static inline void maybe_rearm_tickless_alarm(uint64_t expiry, uint64_t clock_now) {
#ifdef CONFIG_TICKLESS
  bool wait_for_tick = false;
#else // ticks enabled
  uint64_t next_tick = last_tick + TICK_PERIOD;
  bool wait_for_tick = expiry > next_tick;
#endif

  int res;
  if (!wait_for_tick && expiry > clock_now && (next_tickless_expiry == 0 || expiry < next_tickless_expiry)) {
    DPRINTF("rearming tickless alarm to %llu\n", expiry);
    // reprogram the tick source to fire at the next expiry
    if ((res = alarm_source_setval_abs_ns(tickless_source, expiry)) < 0) {
      panic("failed to set tickless source count: %s, value=%llu [err={:err}]", tickless_source->name, expiry, res);
    }
    if ((res = alarm_source_enable(tickless_source)) < 0) {
      panic("failed to enable tickless source: %s [err={:err}]", tickless_source->name, res);
    }
  }
}


static inline void handle_expired_alarms(uint64_t clock_now, uint64_t *next_expiry) {
  alarm_t *alarm;
  uint64_t min_expiry = 0;

  while (true) {
    mtx_spin_lock(&alarm_lock);

    alarm = pending_min();
    if (!alarm) {
      mtx_spin_unlock(&alarm_lock);
      break;
    }

    if (alarm->expires_ns > clock_now) {
      min_expiry = alarm->expires_ns;
      mtx_spin_unlock(&alarm_lock);
      break;
    }

    rb_tree_v2_remove(&pending_alarms, &alarm->expiry_node);
    rb_tree_v2_remove(&alarm_ids, &alarm->id_node);
    alarm_t *next = pending_min();
    if (next)
      min_expiry = next->expires_ns;
    mtx_spin_unlock(&alarm_lock);

    DPRINTF("alarm %d expired\n", alarm->id);
    uint64_t old_expiry = alarm->expires_ns;
    HANDLER_FN(alarm->function)(alarm, alarm->args[0], alarm->args[1], alarm->args[2]);
    if (alarm->expires_ns > old_expiry) {
      // the callback reprogrammed the alarm to fire again
      mtx_spin_lock(&alarm_lock);
      rb_tree_v2_insert(&pending_alarms, &alarm->expiry_node);
      rb_tree_v2_insert(&alarm_ids, &alarm->id_node);
      alarm_t *next = pending_min();
      if (next)
        min_expiry = next->expires_ns;
      mtx_spin_unlock(&alarm_lock);
    } else {
      // the alarm was a one-shot so we can now free it
      alarm_free(&alarm);
    }
  }

  if (next_expiry != NULL) {
    *next_expiry = min_expiry;
  }
}

static void alarm_tick_irq_handler(struct trapframe *frame) {
  thread_t *td = curthread;
  uint64_t clock_now = clock_get_nanos();
//  DPRINTF("tick IRQ [%llu]\n", clock_now);

  last_tick = clock_now;
  uint64_t next_expiry = 0;
  handle_expired_alarms(clock_now, &next_expiry);
  maybe_rearm_tickless_alarm(next_expiry, clock_now);

  if (!TDF_IS_NOPREEMPT(td) && TD_TIMESLICE_EXPIRED(td, clock_now)) {
    DPRINTF("timeslice expired for thread {:td}\n", td);
    // defer the preemption so that it happens on interrupt exit
    set_preempted(true);
  }
}

static void alarm_tickless_irq_handler(struct trapframe *frame) {
  uint64_t clock_now = clock_get_nanos();
  DPRINTF("tickless IRQ [%llu]\n", clock_now);

  uint64_t next_expiry = 0;
  handle_expired_alarms(clock_now, &next_expiry);
  next_tickless_expiry = 0;
  maybe_rearm_tickless_alarm(next_expiry, clock_now);
}

//

void register_alarm_source(alarm_source_t *as) {
  ASSERT(as != NULL);
  as->irq_num = -1;
  as->mode = 0;

  mtx_init(&as->lock, MTX_SPIN|MTX_RECURSIVE, "alarm_source_lock");
  if ((as->cap_flags & ALARM_CAP_ONE_SHOT) == 0 && (as->cap_flags & ALARM_CAP_PERIODIC) == 0) {
    panic("alarm source '%s' must support either one-shot or periodic mode", as->name);
  }

  LIST_ENTRY_INIT(&as->list);
  LIST_ADD(&alarm_sources, as, list);

  DPRINTF("registered alarm source '%s'\n", as->name);
}

//

void alarm_init() {
  rb_tree_v2_init(&pending_alarms, expiry_cmp, expiry_key_cmp);
  rb_tree_v2_init(&alarm_ids, id_cmp, id_key_cmp);
  mtx_init(&alarm_lock, MTX_SPIN, "alarm_lock");

  // TODO: take alarm sources from kernel parameters

  int res;
  if ((tickless_source = alarm_source_get("hpet0")) == NULL) {
    panic("no tickless source found");
  }
  if ((res = alarm_source_init(tickless_source, ALARM_CAP_ONE_SHOT, alarm_tickless_irq_handler)) < 0) {
    panic("failed to initialize alarm source: %s [err={:err}]", tickless_source->name, res);
  }
  // the tickless source is enabled when first programmed

#ifndef CONFIG_TICKLESS // tickless disabled
  if ((tick_source = alarm_source_get("hpet1")) == NULL) {
    if ((tick_source = alarm_source_get("pit")) == NULL) {
      panic("no tick source found");
    }
  }
  if ((res = alarm_source_init(tick_source, ALARM_CAP_PERIODIC, alarm_tick_irq_handler)) < 0) {
    panic("failed to initialize alarm source: %s [err={:err}]", tick_source->name, res);
  }
  if ((res = alarm_source_setval_abs_ns(tick_source, TICK_PERIOD)) < 0) {
    panic("failed to set alarm source value: %s [err={:err}]", tick_source->name, res);
  }
  // the tick source is enabled at the end of kmain
#endif
}

//
// MARK: Alarm Source API
//

alarm_source_t *alarm_source_get(const char *name) {
  alarm_source_t *source;
  LIST_FOREACH(source, &alarm_sources, list) {
    if (strcmp(source->name, name) == 0) {
      return source;
    }
  }
  return NULL;
}

alarm_source_t *alarm_tickless_source() {
  ASSERT(tickless_source != NULL);
  return tickless_source;
}

alarm_source_t *alarm_tick_source() {
  // tick_source can be NULL if tickless mode is enabled
  return tick_source;
}

int alarm_source_init(alarm_source_t *as, int mode, irq_handler_t handler) {
  if (as == NULL) {
    return -EINVAL;
  }

  if (mode != ALARM_CAP_ONE_SHOT && mode != ALARM_CAP_PERIODIC) {
    DPRINTF("invalid alarm mode\n");
    return -EINVAL;
  }
  if ((as->cap_flags & mode) == 0) {
    DPRINTF("alarm source '%s' does not support this mode\n", as->name);
    return -EINVAL;
  }

  int res;
  mtx_spin_lock(&as->lock);
  if (as->mode != 0) {
    DPRINTF("alarm source '%s' already initialized\n", as->name);
    mtx_spin_unlock(&as->lock);
    res = -EBUSY;
    goto ret;
  }

  if ((res = as->init(as, mode, handler)) < 0) {
    DPRINTF("alarm source '%s' failed to initialize: {:err}\n", as->name, res);
    goto ret;
  }

LABEL(ret);
  mtx_spin_unlock(&as->lock);
  return res;
}

int alarm_source_enable(alarm_source_t *as) {
  if (as == NULL) {
    return -1;
  }

  DPRINTF("enabling alarm source '%s'\n", as->name);
  mtx_spin_lock(&as->lock);
  int res = as->enable(as);
  if (res < 0) {
    DPRINTF("alarm source '%s' failed to enable: {:err}\n", as->name, res);
  }
  mtx_spin_unlock(&as->lock);
  return res;
}

int alarm_source_disable(alarm_source_t *as) {
  ASSERT(as != NULL);
  mtx_spin_lock(&as->lock);
  int res = as->disable(as);
  if (res < 0) {
    DPRINTF("alarm source '%s' failed to disable: {:err}\n", as->name, res);
  }
  mtx_spin_unlock(&as->lock);
  return res;
}

int alarm_source_setval_abs_ns(alarm_source_t *as, uint64_t abs_ns) {
  if (as == NULL) {
    return -EINVAL;
  }

  DPRINTF("alarm source '%s' setval_abs_ns: %llu\n", as->name, abs_ns);
  uint64_t value_ns = abs_ns;
  if (!(as->cap_flags & ALARM_CAP_ABSOLUTE)) {
    // correct the value to be relative to current time
    uint64_t clock_now = clock_get_nanos();
    if (abs_ns < clock_now) {
      EPRINTF("alarm source '%s' value %llu is in the past [%llu]\n", as->name, abs_ns, clock_now);
      return -EINVAL;
    }

    value_ns = abs_ns - clock_now;
  }

  uint64_t value = value_ns / as->scale_ns;
  if (value < as->scale_ns || value > as->value_mask) {
    EPRINTF("alarm source '%s' value %llu out of range [min=%u, max=%llu]\n",
            as->name, value, as->scale_ns, as->value_mask);
    return -ERANGE;
  }

  mtx_spin_lock(&as->lock);
  int res = as->setval(as, value);
  if (res < 0) {
    DPRINTF("alarm source '%s' failed to set value: {:err}\n", as->name, res);
  }
  mtx_spin_unlock(&as->lock);
  return res;
}

int alarm_source_setval_rel_ns(alarm_source_t *as, uint64_t rel_ns) {
  if (as == NULL) {
    return -EINVAL;
  }

  uint64_t value = rel_ns / as->scale_ns;
  if (as->cap_flags & ALARM_CAP_ABSOLUTE) {
    // correct the value to be an absolute timestamp
    value += clock_get_nanos();
  }

  if (value < as->scale_ns || value > as->value_mask) {
    DPRINTF("alarm source '%s' value %llu out of range [min=%u, max=%llu]\n",
            as->name, value, as->scale_ns, as->value_mask);
    return -ERANGE;
  }

  mtx_spin_lock(&as->lock);
  int res = as->setval(as, rel_ns / as->scale_ns);
  if (res < 0) {
    DPRINTF("alarm source '%s' failed to set value: {:err}\n", as->name, res);
  }
  mtx_spin_unlock(&as->lock);
  return res;
}

//
// MARK: Alarm API
//

alarm_t *alarm_alloc_absolute(uint64_t clock_ns, struct callback cb) {
  if (cb.function == 0) {
    DPRINTF("alarm_alloc: callback function is not set\n");
    return NULL;
  }

  alarm_t *alarm = pool_alloc(alarm_pool, sizeof(alarm_t));
  if (alarm == NULL) {
    DPRINTF("alarm_alloc: failed to allocate alarm\n");
    return NULL;
  }

  alarm->id = atomic_fetch_add(&next_alarm_id, 1);
  alarm->expires_ns = clock_ns;
  alarm->function = cb.function;
  memcpy(alarm->args, cb.args, sizeof(cb.args));
  return alarm;
}

alarm_t *alarm_alloc_relative(uint64_t offset_ns, struct callback cb) {
  return alarm_alloc_absolute(clock_get_nanos() + offset_ns, cb);
}

void alarm_free(alarm_t **alarmp) {
  alarm_t *alarm = moveptr(*alarmp);
  if (alarm == NULL) {
    return;
  }
  pool_free(alarm_pool, alarm);
}

id_t alarm_register(alarm_t *alarm) {
  if (alarm->expires_ns == 0) {
    DPRINTF("alarm_register: alarm %d has an invalid expiry time\n", alarm->id);
    return 0;
  }

  uint64_t clock_now = clock_get_nanos();
  mtx_spin_lock(&alarm_lock);
  rb_tree_v2_insert(&pending_alarms, &alarm->expiry_node);
  rb_tree_v2_insert(&alarm_ids, &alarm->id_node);
  alarm_t *min = pending_min();
  maybe_rearm_tickless_alarm(min->expires_ns + MS_TO_NS(2), clock_now);
  mtx_spin_unlock(&alarm_lock);
  DPRINTF("alarm_register: alarm %d expires at %llu\n", alarm->id, alarm->expires_ns);
  return alarm->id;
}

int alarm_unregister(id_t alarm_id, struct callback *callback) {
  mtx_spin_lock(&alarm_lock);
  alarm_t *alarm = alarm_find_by_id(alarm_id);
  if (!alarm) {
    mtx_spin_unlock(&alarm_lock);
    return -ENOENT;
  }

  rb_tree_v2_remove(&pending_alarms, &alarm->expiry_node);
  rb_tree_v2_remove(&alarm_ids, &alarm->id_node);
  mtx_spin_unlock(&alarm_lock);

  DPRINTF("alarm_unregister: alarm %d unregistered\n", alarm->id);
  if (callback) {
    callback->function = alarm->function;
    memcpy(callback->args, alarm->args, sizeof(alarm->args));
  }
  alarm_free(&alarm);
  return 0;
}

//
// MARK: Sleep API
//

static void alarm_cb_wakeup(alarm_t *alarm) {
  struct waitqueue *waitq = waitq_lookup(alarm);
  if (waitq == NULL) {
    DPRINTF("alarm_cb_wakeup: waitqueue not found\n");
    return;
  }
  waitq_broadcast(waitq);
}

int alarm_sleep_ms(uint64_t ms) {
  uint64_t clock_now = clock_get_nanos();
  alarm_t *alarm = alarm_alloc_absolute(clock_now + MS_TO_NS(ms), alarm_cb(alarm_cb_wakeup, NULL));
  ASSERT(alarm != NULL);

  if (alarm_register(alarm) == 0) {
    DPRINTF("alarm_sleep_ms: failed to register alarm\n");
    alarm_free(&alarm);
    return -EINVAL;
  }

  struct waitqueue *waitq = waitq_lookup_or_default(WQ_SLEEP, alarm, curthread->own_waitq);
  waitq_wait(waitq, "sleeping");
  return 0;
}

int alarm_sleep_ns(uint64_t ns) {
  uint64_t start_time = clock_get_nanos();
  uint64_t target_time = start_time + ns;
  alarm_t *alarm = alarm_alloc_absolute(target_time, alarm_cb(alarm_cb_wakeup, NULL));
  ASSERT(alarm != NULL);

  id_t alarm_id = alarm_register(alarm);
  if (alarm_id == 0) {
    DPRINTF("alarm_sleep_ns: failed to register alarm\n");
    alarm_free(&alarm);
    return -EINVAL;
  }

  struct waitqueue *waitq = waitq_lookup_or_default(WQ_SLEEP, alarm, curthread->own_waitq);
  int ret = waitq_wait_sig(waitq, "nanosleep");

  // unregister the alarm if it hasn't fired yet
  if (ret != 0) {
    alarm_unregister(alarm_id, NULL);
  }

  return ret;
}

//
// MARK: System Calls
//

static void alarm_cb_deliver_signal(alarm_t *alarm, pid_t pid) {
  int res;
  proc_t *proc = proc_lookup(pid);
  if (proc == NULL) {
    DPRINTF("alarm_cb_deliver_signal: process %d not found\n", pid);
    return;
  }

  if ((res = proc_signal(proc, &(siginfo_t){.si_signo = SIGALRM})) < 0) {
    DPRINTF("alarm_cb_deliver_signal: failed to deliver signal: {:err}\n", res);
  }

  pr_putref(&proc);
}

static void alarm_cb_handle_itimer(alarm_t *alarm, pid_t pid, int which) {
  DPRINTF("alarm_cb_handle_itimer: called for pid %d, which %d\n", pid, which);
  proc_t *proc = proc_lookup(pid);
  if (proc == NULL) {
    DPRINTF("alarm_cb_handle_itimer: process %d not found\n", pid);
    return;
  }

  int res;
  DPRINTF("alarm_cb_handle_itimer: calling proc_signal for SIGALRM\n");
  if ((res = proc_signal(proc, &(siginfo_t){.si_signo = SIGALRM})) < 0) {
    DPRINTF("alarm_cb_handle_itimer: failed to deliver signal: {:err}\n", res);
    goto done;
  }
  DPRINTF("alarm_cb_handle_itimer: proc_signal succeeded\n");

  // reload the timer if it is periodic
  struct itimerval *itv = &proc->itimer_vals[which];
  if (!timeval_is_zero(&itv->it_interval)) {
    // update the alarm to the next expiry time
    uint64_t now = clock_get_nanos();
    uint64_t expires_ns = now + timeval_to_nanos(&itv->it_interval);
    // reprogram alarm to fire again
    alarm->expires_ns = expires_ns;
  }

LABEL(done);
  pr_putref(&proc);
}

static void itimer_get_update(int which, struct itimerval *curr_value, const struct itimerval *new_value) {
  ASSERT(which == ITIMER_REAL);
  pr_lock_assert(curproc, MA_OWNED);

  proc_t *proc = curproc;
  struct itimerval *itv = &proc->itimer_vals[which];

  // return the current timer value (if specified)
  if (curr_value != NULL) {
    if (timeval_is_zero(&itv->it_value)) {
      // no timer set
      curr_value->it_value = itv->it_value;
      curr_value->it_interval = itv->it_interval;
    } else {
      // compute the time remaining
      struct timeval now = clock_micro_time();
      struct timeval expiry_time = timeval_diff(&now, &itv->it_value);
      curr_value->it_value = expiry_time;
      curr_value->it_interval = itv->it_interval;
    }
  }

  // set the new timer value (if specified)
  if (new_value != NULL) {
    // cancel previous alarm if it exists
    if (proc->itimer_alarms[which] > 0) {
      alarm_unregister(proc->itimer_alarms[which], NULL);
      proc->itimer_alarms[which] = 0;
    }

    proc->itimer_vals[which] = *new_value;
    if (!timeval_is_zero(&new_value->it_value)) {
      // register a new alarm
      uint64_t expires_ns = clock_get_nanos() + timeval_to_nanos(&new_value->it_value);
      alarm_t *alarm = alarm_alloc_absolute(expires_ns, alarm_cb(
        alarm_cb_handle_itimer,
        proc->pid,
        which
      ));
      ASSERT(alarm != NULL);

      id_t alarm_id = alarm_register(alarm);
      if (alarm_id == 0) {
        DPRINTF("itimer_get_update: failed to register alarm:\n");
        alarm_free(&alarm);
        return;
      }

      proc->itimer_alarms[which] = alarm_id;
    }
  }
}

DEFINE_SYSCALL(alarm, int, unsigned int seconds) {
  DPRINTF("syscall: alarm seconds=%u\n", seconds);
  proc_t *proc = curproc;
  pr_lock(proc);

  // cancel any existing pending alarm
  if (proc->pending_alarm > 0) {
    alarm_unregister(proc->pending_alarm, NULL);
    proc->pending_alarm = 0;
  }

  if (seconds == 0) {
    goto done; // just cancel the alarm
  }

  // create a new alarm
  uint64_t expires_ns = SEC_TO_NS((uint64_t)seconds);
  alarm_t *alarm = alarm_alloc_relative(expires_ns, alarm_cb(alarm_cb_deliver_signal, proc->pid));
  ASSERT(alarm != NULL);
  proc->pending_alarm = alarm_register(alarm);
  if (proc->pending_alarm < 0) {
    DPRINTF("alarm: failed to register alarm: {:err}\n", proc->pending_alarm);
    alarm_free(&alarm);
    proc->pending_alarm = 0;
    pr_unlock(proc);
    return -EINVAL;
  }

LABEL(done);
  pr_unlock(proc);
  return 0;
}

DEFINE_SYSCALL(getitimer, int, int which, struct itimerval *curr_value) {
  DPRINTF("syscall: getitimer which=%d curr_value=%p\n", which, curr_value);
  proc_t *proc = curproc;
  if (which < ITIMER_REAL || which >= ITIMER_PROF) {
    return -EINVAL;
  } else if (which != ITIMER_REAL) {
    return -ENOTSUP;
  }

  if (vm_validate_ptr((uintptr_t) curr_value, /*write=*/true) < 0) {
    return -EFAULT;
  }

  pr_lock(proc);
  itimer_get_update(which, curr_value, /*new_value=*/NULL);
  pr_unlock(proc);
  return 0;
}

DEFINE_SYSCALL(setitimer, int, int which, const struct itimerval *new_value, struct itimerval *old_value) {
  DPRINTF("syscall: setitimer which=%d, new_value=%p, old_value=%p\n", which, new_value, old_value);
  proc_t *proc = curproc;
  if (which < ITIMER_REAL || which >= ITIMER_PROF) {
    return -EINVAL;
  } else if (which != ITIMER_REAL) {
    return -ENOTSUP;
  }

  if ((vm_validate_ptr((uintptr_t) new_value, /*write=*/false) < 0) ||
      (old_value != NULL && vm_validate_ptr((uintptr_t) old_value, /*write=*/true) < 0)) {
    return -EFAULT;
  }

  pr_lock(proc);
  itimer_get_update(which, old_value, new_value);
  pr_unlock(proc);
  return 0;
}

DEFINE_SYSCALL(nanosleep, int, const struct timespec *duration, struct timespec *rem) {
  DPRINTF("syscall: nanosleep duration=%p rem=%p\n", duration, rem);
  if (vm_validate_ptr((uintptr_t) duration, /*write=*/false) < 0) {
    return -EFAULT;
  }
  if (rem != NULL && vm_validate_ptr((uintptr_t) rem, /*write=*/true) < 0) {
    return -EFAULT;
  }
  
  // validate timespec
  if (duration->tv_sec < 0 || duration->tv_nsec < 0 || duration->tv_nsec >= NS_PER_SEC) {
    return -EINVAL;
  }
  
  uint64_t sleep_ns = timespec_to_nanos((struct timespec *)duration);
  if (sleep_ns == 0) {
    return 0; // nothing to sleep
  }
  
  uint64_t start_time = clock_get_nanos();
  int ret = alarm_sleep_ns(sleep_ns);
  
  if (ret == -EINTR && rem != NULL) {
    // calculate remaining time
    uint64_t elapsed_ns = clock_get_nanos() - start_time;
    if (elapsed_ns < sleep_ns) {
      uint64_t remaining_ns = sleep_ns - elapsed_ns;
      *rem = timespec_from_nanos(remaining_ns);
    } else {
      *rem = timespec_zero;
    }
  }
  
  return ret;
}
