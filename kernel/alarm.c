//
// Created by Aaron Gill-Braun on 2025-04-25.
//

#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/proc.h>
#include <kernel/irq.h>
#include <kernel/sched.h>
#include <kernel/mm/vmalloc.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <bitmap.h>
#include <rb_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("alarm: " x, ##__VA_ARGS__)

#define HANDLER_FN(fn) ((void (*)(alarm_t *, void *, void *, void *))(fn))

static LIST_HEAD(alarm_source_t) alarm_sources;

// TODO: switch to a callwheel based approach for storing alarms
//       https://people.freebsd.org/~davide/asia/calloutng.pdf
static rb_tree_t *pending_alarms;
// TODO: switch to a better data structure for mapping ids -> expiries
static rb_tree_t *alarm_expiries;
// spinlock for the alarm tree
static mtx_t alarm_lock;
// alarm id atomic "allocator"
static id_t next_alarm_id = 1; // id==0 is invalid


static void alarm_irq_handler(struct trapframe *frame) {
  thread_t *td = curthread;
  uint64_t clock_now = clock_get_nanos();
  DPRINTF("alarm IRQ [%llu]\n", clock_now);

  // handle any expired alarms
  alarm_t *alarm;
  while ((alarm = pending_alarms->min->data) && alarm->expires_ns <= clock_now) {
    mtx_spin_lock(&alarm_lock);
    rb_tree_delete_node(pending_alarms, pending_alarms->min);
    rb_tree_delete(alarm_expiries, alarm->id);
    mtx_spin_unlock(&alarm_lock);

    DPRINTF("alarm %d expired\n", alarm->id);
    uint64_t old_expiry = alarm->expires_ns;
    HANDLER_FN(alarm->function)(alarm, alarm->args[0], alarm->args[1], alarm->args[2]);
    if (alarm->expires_ns > old_expiry) {
      // the callback reprogrammed the alarm to fire again
      mtx_spin_lock(&alarm_lock);
      rb_tree_insert(pending_alarms, alarm->expires_ns, alarm);
      rb_tree_insert(alarm_expiries, alarm->id, (void *)alarm->expires_ns);
      mtx_spin_unlock(&alarm_lock);
    } else {
      // the alarm was a one-shot so we can now free it
      alarm_free(&alarm);
    }
  }

  if (!TDF_IS_NOPREEMPT(td) && TD_TIMESLICE_EXPIRED(td, clock_now)) {
    DPRINTF("timeslice expired for thread {:td}\n", td);
    // defer the preemption so that it happens on interrupt exit
    set_preempted(true);
  }
}

//

void register_alarm_source(alarm_source_t *as) {
  ASSERT(as != NULL);
  as->irq_num = -1;

  mtx_init(&as->lock, MTX_SPIN, "alarm_source_lock");
  if ((as->cap_flags & ALARM_CAP_ONE_SHOT) == 0 && (as->cap_flags & ALARM_CAP_PERIODIC) == 0) {
    panic("alarm source '%s' must support either one-shot or periodic mode", as->name);
  }

  LIST_ENTRY_INIT(&as->list);
  LIST_ADD(&alarm_sources, as, list);

  DPRINTF("registered alarm source '%s'\n", as->name);
}

alarm_source_t *alarm_source_get(const char *name) {
  alarm_source_t *source;
  LIST_FOREACH(source, &alarm_sources, list) {
    if (strcmp(source->name, name) == 0) {
      return source;
    }
  }
  return NULL;
}

void alarm_init() {
  pending_alarms = create_rb_tree();
  if (pending_alarms == NULL) {
    panic("failed to create alarm tree");
  }
  alarm_expiries = create_rb_tree();
  if (alarm_expiries == NULL) {
    panic("failed to create alarm expiries tree");
  }
  mtx_init(&alarm_lock, MTX_SPIN, "alarm_lock");

  // TODO: take alarm source from kernel parameters
  alarm_source_t *tick_as = alarm_source_get("hpet0");
  if (tick_as == NULL) {
    panic("no alarm source found");
  }

  int res;
  if ((res = tick_as->init(tick_as, ALARM_CAP_PERIODIC, alarm_irq_handler)) < 0) {
    panic("failed to initialize alarm source: %s [err={:err}]", tick_as->name, res);
  }
  if ((res = alarm_source_setval_ns(tick_as, TICK_PERIOD)) < 0) {
    panic("failed to set alarm source value: %s [err={:err}]", tick_as->name, res);
  }
  if ((res = tick_as->enable(tick_as)) < 0) {
    panic("failed to enable alarm source: %s [err={:err}]", tick_as->name, res);
  }
}

//
// MARK: Alarm Source API
//

int alarm_source_init(alarm_source_t *as, int mode, irq_handler_t handler) {
  ASSERT(as != NULL);
  if (mode != ALARM_CAP_ONE_SHOT && mode != ALARM_CAP_PERIODIC) {
    DPRINTF("invalid alarm mode\n");
    return -EINVAL;
  } else if ((as->cap_flags & mode) == 0) {
    DPRINTF("alarm source '%s' does not support this mode\n", as->name);
    return -EINVAL;
  }

  int res;
  mtx_spin_lock(&as->lock);
  if (as->mode != 0) {
    DPRINTF("alarm source '%s' already initialized\n", as->name);
    mtx_unlock(&as->lock);
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
  ASSERT(as != NULL);
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

int alarm_source_setval_ns(alarm_source_t *as, uint64_t ns) {
  ASSERT(as != NULL);
  if (as->setval == NULL) {
    return -ENOTSUP;
  }

  uint64_t value = ns / as->scale_ns;
  if (value < as->scale_ns || value > as->value_mask) {
    DPRINTF("alarm source '%s' value %llu out of range [min=%llu, max=%llu]\n",
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

//
// MARK: Alarm API
//

alarm_t *alarm_alloc_absolute(uint64_t clock_ns, struct callback cb) {
  if (cb.function == 0) {
    DPRINTF("alarm_alloc: callback function is not set\n");
    return NULL;
  }

  alarm_t *alarm = kmallocz(sizeof(alarm_t));
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
  kfree(alarm);
}

id_t alarm_register(alarm_t *alarm) {
  if (alarm->expires_ns == 0) {
    DPRINTF("alarm_register: alarm %d has an invalid expiry time\n", alarm->id);
    return 0;
  }

  mtx_spin_lock(&alarm_lock);
  rb_tree_insert(pending_alarms, alarm->expires_ns, alarm);
  rb_tree_insert(alarm_expiries, alarm->id, (void *)alarm->expires_ns);
  mtx_spin_unlock(&alarm_lock);
  DPRINTF("alarm_register: alarm %d expires at %llu\n", alarm->id, alarm->expires_ns);
  return alarm->id;
}

int alarm_unregister(id_t alarm_id) {
  mtx_spin_lock(&alarm_lock);
  id_t expires_ns = (id_t)(uintptr_t)rb_tree_find(alarm_expiries, alarm_id);
  if (expires_ns == 0) {
    goto not_found;
  }

  // locate the alarm expiry by the given id
  rb_node_t *node = rb_tree_find_node(pending_alarms, expires_ns);
  if (node == NULL) {
    goto not_found;
  }

  // there may be multiple alarms with the same expiration time
  // locate the right one
  while (node != pending_alarms->nil && ((alarm_t*)node->data)->id != alarm_id) {
    node = node->next;
  }
  if (node == pending_alarms->nil) {
    goto not_found;
  }

  // remove it
  alarm_t *alarm = node->data;
  rb_tree_delete_node(pending_alarms, node);
  rb_tree_delete(alarm_expiries, alarm_id);
  mtx_spin_unlock(&alarm_lock);

  DPRINTF("alarm_unregister: alarm %d unregistered\n", alarm->id);
  alarm_free(&alarm);
  return 0;

LABEL(not_found);
  DPRINTF("alarm_unregister: alarm %d not found\n", alarm_id);
  mtx_spin_unlock(&alarm_lock);
  return -ENOENT;
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
  alarm_t *alarm = alarm_alloc_relative(MS_TO_NS(ms), alarm_cb(alarm_cb_wakeup, NULL));
  ASSERT(alarm != NULL);

  struct waitqueue *waitq = waitq_lookup_or_default(WQ_SLEEP, alarm, curthread->own_waitq);
  if (alarm_register(alarm) == 0) {
    DPRINTF("alarm_sleep_ms: failed to register alarm\n");
    alarm_free(&alarm);
    return -EINVAL;
  }
  waitq_add(waitq, "sleeping");
  return 0;
}

//
// MARK: System Calls
//

static void alarm_cb_deliver_signal(alarm_t *alarm, pid_t pid) {
  int res;
  if ((res = pid_signal(pid, SIGALRM, 0, (union sigval){0})) < 0) {
    DPRINTF("alarm_cb_deliver_signal: failed to deliver signal: {:err}\n", res);
  }
}

static void alarm_cb_handle_itimer(alarm_t *alarm, pid_t pid, int which) {
  proc_t *proc = proc_lookup(pid);
  if (proc == NULL) {
    DPRINTF("alarm_cb_handle_itimer: process %d not found\n", pid);
    return;
  }

  int res;
  if ((res = proc_signal(proc, SIGALRM, 0, (union sigval){0})) < 0) {
    DPRINTF("alarm_cb_handle_itimer: failed to deliver signal: {:err}\n", res);
    goto done;
  }

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
      alarm_unregister(proc->itimer_alarms[which]);
      proc->itimer_alarms[which] = 0;
    }

    proc->itimer_vals[which] = *new_value;
    if (!timeval_is_zero(&new_value->it_value)) {
      // register a new alarm
      uint64_t expires_ns = timeval_to_nanos(&new_value->it_value);
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
    alarm_unregister(proc->pending_alarm);
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

  if (vm_validate_user_ptr((uintptr_t)curr_value, /*write=*/true) < 0) {
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

  if ((vm_validate_user_ptr((uintptr_t) new_value, /*write=*/false) < 0) ||
      (old_value == NULL || vm_validate_user_ptr((uintptr_t) old_value, /*write=*/true) < 0)) {
    return -EFAULT;
  }

  pr_lock(proc);
  itimer_get_update(which, old_value, new_value);
  pr_unlock(proc);
  return 0;
}
