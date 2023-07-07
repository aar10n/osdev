//
// Created by Aaron Gill-Braun on 2022-06-25.
//

#ifndef KERNEL_CPU_PER_CPU_H_BASE
#define KERNEL_CPU_PER_CPU_H_BASE

#ifndef __PER_CPU_BASE__
#include <kernel/types.h>
#endif

struct thread;
struct process;
struct address_space;
struct sched;
struct cpu_info;

#define PERCPU_SIZE 0x1000
typedef struct __attribute__((aligned(128))) per_cpu {
  uint64_t self;
  uint16_t id;
  uint16_t apic_id;
  uint32_t errno;
  struct thread *thread;
  struct process *process;
  uint64_t kernel_sp;
  uint64_t user_sp;
  uint64_t rflags;

  uint32_t irq_level;
  uint32_t : 32;

  struct address_space *address_space;
  struct sched *sched;
  struct cpu_info *cpu_info;
  void *cpu_gdt;
  void *cpu_tss;
} per_cpu_t;
_Static_assert(sizeof(per_cpu_t) <= PERCPU_SIZE, "");
_Static_assert(offsetof(per_cpu_t, thread) == 0x10, "");
_Static_assert(offsetof(per_cpu_t, process) == 0x18, "");

#define __percpu_get_u16(offset) ({ register uint16_t v; __asm("mov %0, gs:%1" : "=r" (v) : "i" (offset)); v; })
#define __percpu_get_u32(offset) ({ register uint32_t v; __asm("mov %0, gs:%1" : "=r" (v) : "i" (offset)); v; })
#define __percpu_get_u64(offset) ({ register uint64_t v; __asm("mov %0, gs:%1" : "=r" (v) : "i" (offset)); v; })
#define __percpu_set_u32(offset, val) ({ register uint32_t v = (uint32_t)(val); __asm("mov gs:%0, %1" : : "i" (offset), "r" (v)); })
#define __percpu_set_u64(offset, val) ({ register uint64_t v = (uint64_t)(val); __asm("mov gs:%0, %1" : : "i" (offset), "r" (v)); })

#define __percpu_get_self() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, self)))
#define __percpu_get_id() ((uint8_t) __percpu_get_u16(offsetof(per_cpu_t, id)))
#define __percpu_get_apic_id() ((uint8_t) __percpu_get_u16(offsetof(per_cpu_t, apic_id)))
#define __percpu_get_errno() ((int) __percpu_get_u32(offsetof(per_cpu_t, errno)))
#define __percpu_get_thread() ((struct thread *) __percpu_get_u64(offsetof(per_cpu_t, thread)))
#define __percpu_get_process() ((struct process *) __percpu_get_u64(offsetof(per_cpu_t, process)))
#define __percpu_get_kernel_sp() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, kernel_sp)))
#define __percpu_get_user_sp() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, user_sp)))
#define __percpu_get_rflags() __percpu_get_u64(offsetof(per_cpu_t, rflags))
#define __percpu_get_irq_level() __percpu_get_u32(offsetof(per_cpu_t, irq_level))
#define __percpu_get_address_space() ((struct address_space *) __percpu_get_u64(offsetof(per_cpu_t, address_space)))
#define __percpu_get_sched() ((struct sched *) __percpu_get_u64(offsetof(per_cpu_t, sched)))
#define __percpu_get_cpu_info() ((struct cpu_info *) __percpu_get_u64(offsetof(per_cpu_t, cpu_info)))
#define __percpu_get_cpu_tss() ((void *) __percpu_get_u64(offsetof(per_cpu_t, cpu_tss)))

#define __percpu_set_errno(value) __percpu_set_u32(offsetof(per_cpu_t, errno), value)
#define __percpu_set_thread(value) __percpu_set_u64(offsetof(per_cpu_t, thread), (uintptr_t)(value))
#define __percpu_set_process(value) __percpu_set_u64(offsetof(per_cpu_t, process), (uintptr_t)(value))
#define __percpu_set_rflags(value) __percpu_set_u64(offsetof(per_cpu_t, rflags), value)
#define __percpu_set_address_space(value) __percpu_set_u64(offsetof(per_cpu_t, address_space), (uintptr_t)(value))
#define __percpu_set_sched(value) __percpu_set_u64(offsetof(per_cpu_t, sched), (uintptr_t)(value))
#define __percpu_set_cpu_info(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_info), (uintptr_t)(value))
#define __percpu_set_cpu_gdt(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_gdt), (void *)(value))
#define __percpu_set_cpu_tss(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_tss), (void *)(value))

#define __percpu_inc_irq_level() \
  ({                             \
    uint32_t l = __percpu_get_irq_level(); \
    uint32_t res = l + 1;        \
    if (res < l) l = UINT32_MAX; \
    __percpu_set_u32(offsetof(per_cpu_t, irq_level), l); \
  })
#define __percpu_dec_irq_level() \
  ({                             \
    uint32_t l = __percpu_get_irq_level(); \
    uint32_t res = l - 1;        \
    if (res > l) l = 0; \
    __percpu_set_u32(offsetof(per_cpu_t, irq_level), l); \
  })

#define __percpu_struct_ptr() ((per_cpu_t *) __percpu_get_self())
#define __percpu_field_ptr(field) (&__percpu_struct_ptr()->field)

#define PERCPU_ID __percpu_get_id()
#define PERCPU_APIC_ID __percpu_get_apic_id()
#define PERCPU_THREAD __percpu_get_thread()
#define PERCPU_PROCESS __percpu_get_process()
#define PERCPU_RFLAGS __percpu_get_rflags()
#define PERCPU_ADDRESS_SPACE __percpu_get_address_space()
#define PERCPU_SCHED __percpu_get_sched()
#define PERCPU_CPU_INFO __percpu_get_cpu_info()

#define PERCPU_SET_THREAD(value) __percpu_set_thread(value)
#define PERCPU_SET_PROCESS(value) __percpu_set_process(value)
#define PERCPU_SET_RFLAGS(value) __percpu_set_rflags(value)
#define PERCPU_SET_ADDRESS_SPACE(value) __percpu_set_address_space(value)
#define PERCPU_SET_SCHED(value) __percpu_set_sched(value)
#define PERCPU_SET_CPU_INFO(value) __percpu_set_cpu_info(value)
#define PERCPU_SET_CPU_GDT(value) __percpu_set_cpu_gdt(value)
#define PERCPU_SET_CPU_TSS(value) __percpu_set_cpu_tss(value)

#endif

//

#ifndef __PER_CPU_BASE__
#ifndef KERNEL_CPU_PER_CPU_H_PROTO
#define KERNEL_CPU_PER_CPU_H_PROTO

per_cpu_t *percpu_alloc_area(uint16_t id, uint8_t apic_id);

#endif
#endif
