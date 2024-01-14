//
// Created by Aaron Gill-Braun on 2023-12-23.
//

#ifndef KERNEL_ATOMIC_H
#define KERNEL_ATOMIC_H

#define atomic_ordered_cmpxchg(ptr, old, _new, order) ({ \
    __typeof__(*(ptr)) __old = (old); \
    __atomic_compare_exchange_n(ptr, &__old, _new, false, order, order); \
})
#define atomic_ordered_cmpxchgp(ptr, old, _newp, order) ({ \
    __typeof__(*(ptr)) __old = (old); \
    __atomic_compare_exchange(ptr, &__old, _newp, false, order, order); \
})

#define atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
#define atomic_load_relaxed(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)
#define atomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_store_release(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)

#define atomic_fetch_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_sub(ptr, val) __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST)

#define atomic_fetch_and(ptr, val) __atomic_fetch_and(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_or(ptr, val) __atomic_fetch_or(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_xor(ptr, val) __atomic_fetch_xor(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_nand(ptr, val) __atomic_fetch_nand(ptr, val, __ATOMIC_SEQ_CST)

#define atomic_xchg(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST)


/*
 * Atomic compare and exchange.
 *
 * cmpxchg:
 *   if (*ptr == old)
 *     *ptr = _new;
 *
 * cmpxchgp:
 *   if (*ptr == *old)
 *     *ptr = *_newp;
 *   else
 *     *old = *ptr;
 *
 * Returns true if the exchange was successful.
 */
#define atomic_cmpxchg(ptr, old, _new) atomic_ordered_cmpxchg(ptr, old, _new, __ATOMIC_SEQ_CST)
#define atomic_cmpxchg_acq(ptr, old, _new) atomic_ordered_cmpxchg(ptr, old, _new, __ATOMIC_ACQUIRE)
#define atomic_cmpxchg_rel(ptr, old, _new) atomic_ordered_cmpxchg(ptr, old, _new, __ATOMIC_RELEASE)
#define atomic_cmpxchgp(ptr, old, _newp) atomic_ordered_cmpxchgp(ptr, old, _newp, __ATOMIC_SEQ_CST)
#define atomic_cmpxchgp_acq(ptr, old, _newp) atomic_ordered_cmpxchgp(ptr, old, _newp, __ATOMIC_ACQUIRE)

#define atomic_thread_fence() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define atomic_signal_fence() __atomic_signal_fence(__ATOMIC_SEQ_CST)

#endif
