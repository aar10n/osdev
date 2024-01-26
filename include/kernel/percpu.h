//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#ifndef KERNEL_PERCPU_H
#define KERNEL_PERCPU_H

#ifndef __PERCPU_BASE__
#include <kernel/types.h>
#endif

struct cpu_info;
struct thread;
struct proc;
struct sched;
struct address_space;
struct lock_claim_list;

struct percpu {
  uint32_t id;
  uint32_t : 32;
  uintptr_t self;
  struct address_space *space;
  struct thread *thread;
  struct proc *proc;
  struct sched *sched;
  struct cpu_info *info;
  struct lock_claim_list *spin_claims;

  uint64_t rflags;
  void *gdt;
  void *tss;
} __attribute__((aligned(128)));
_Static_assert(sizeof(struct percpu) <= 0x1000, "percpu too big");
_Static_assert(offsetof(struct percpu, id) == 0x00, "percpu id offset");
_Static_assert(offsetof(struct percpu, self) == 0x08, "percpu self offset");
_Static_assert(offsetof(struct percpu, space) == 0x10, "percpu space offset");

#define __percpu_get_u32(member) ({ register uint32_t __v; __asm("mov %0, gs:%1" : "=r" (__v) : "i" (offsetof(struct percpu, member))); __v; })
#define __percpu_get_u64(member) ({ register uint64_t __v; __asm("mov %0, gs:%1" : "=r" (__v) : "i" (offsetof(struct percpu, member))); __v; })
#define __percpu_set_u32(member, val) ({ register uint32_t __v = (uint32_t)(val); __asm("mov gs:%0, %1" : : "i" (offsetof(struct percpu, member)), "r" (__v)); })
#define __percpu_set_u64(member, val) ({ register uint64_t __v = (uint64_t)(val); __asm("mov gs:%0, %1" : : "i" (offsetof(struct percpu, member)), "r" (__v)); })

//

#define PERCPU_ID ((uint8_t) __percpu_get_u32(id))
#define PERCPU_AREA ((struct percpu *) __percpu_get_u64(self))
#define PERCPU_RFLAGS __percpu_get_u64(rflags)

#define PERCPU_SET_RFLAGS(val) __percpu_set_u64(rflags, val)

#define curcpu_id ((uint8_t) __percpu_get_u32(id))
#define curcpu_is_boot (curcpu_id == 0)

#define curcpu_area ((struct percpu *) __percpu_get_u64(self))
#define curcpu_info ((struct cpu_info *) __percpu_get_u64(info))
#define curcpu_spin_claims ((struct lock_claim_list *) __percpu_get_u64(spin_claims))

#define curspace ((struct address_space *) __percpu_get_u64(space))
#define curthread ((struct thread *) __percpu_get_u64(thread))
#define curproc ((struct proc *) __percpu_get_u64(proc))
#define cursched ((struct sched *) __percpu_get_u64(sched))

#define set_curspace(s) __percpu_set_u64(space, s)
#define set_curthread(t) __percpu_set_u64(thread, t)
#define set_curproc(p) __percpu_set_u64(proc, p)
#define set_cursched(s) __percpu_set_u64(sched, s)

#endif

#ifndef __PERCPU_BASE__
#ifndef __PERCPU_PROTO__
#define __PERCPU_PROTO__
struct percpu *percpu_alloc_area(uint32_t id);
#endif
#endif
