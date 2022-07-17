//
// Created by Aaron Gill-Braun on 2022-06-25.
//

#ifndef KERNEL_CPU_PER_CPU_H
#define KERNEL_CPU_PER_CPU_H

#ifndef __PER_CPU_BASE__
#include <abi/types.h>
#endif

struct thread;
struct process;
struct address_space;
struct scheduler;
struct cpu_info;

#define PER_CPU_SIZE 0x1000
typedef struct __attribute__((aligned(128))) per_cpu {
  uint64_t self;
  uint32_t id;
  uint32_t errno;
  struct thread *thread;
  struct process *process;
  uint64_t kernel_sp;
  uint64_t user_sp;
  uint64_t rflags;

  struct address_space *address_space;
  struct scheduler *scheduler;
  struct cpu_info *cpu_info;
  void *cpu_gdt;
  void *cpu_idt;
} per_cpu_t;
_Static_assert(sizeof(per_cpu_t) <= PER_CPU_SIZE, "");

#define __percpu_get_u32(offset) ({ register uint32_t v; asm("mov %0, gs:%1" : "=r" (v) : "i" (offset)); v; })
#define __percpu_get_u64(offset) ({ register uint64_t v; asm("mov %0, gs:%1" : "=r" (v) : "i" (offset)); v; })
#define __percpu_set_u32(offset, val) ({ register uint32_t v = (uint32_t)(val); asm("mov gs:%0, %1" : : "i" (offset), "r" (v)); })
#define __percpu_set_u64(offset, val) ({ register uint64_t v = (uint64_t)(val); asm("mov gs:%0, %1" : : "i" (offset), "r" (v)); })

#define __percpu_get_self() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, self)))
#define __percpu_get_id() ((uint32_t) __percpu_get_u32(offsetof(per_cpu_t, id)))
#define __percpu_get_errno() ((int) __percpu_get_u32(offsetof(per_cpu_t, errno)))
#define __percpu_get_thread() ((struct thread *) __percpu_get_u64(offsetof(per_cpu_t, thread)))
#define __percpu_get_process() ((struct process *) __percpu_get_u64(offsetof(per_cpu_t, process)))
#define __percpu_get_kernel_sp() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, kernel_sp)))
#define __percpu_get_user_sp() ((uintptr_t) __percpu_get_u64(offsetof(per_cpu_t, user_sp)))
#define __percpu_get_rflags() __percpu_get_u64(offsetof(per_cpu_t, rflags))
#define __percpu_get_address_space() ((struct address_space *) __percpu_get_u64(offsetof(per_cpu_t, address_space)))
#define __percpu_get_scheduler() ((struct scheduler *) __percpu_get_u64(offsetof(per_cpu_t, scheduler)))
#define __percpu_get_cpu_info() ((struct cpu_info *) __percpu_get_u64(offsetof(per_cpu_t, cpu_info)))

#define __percpu_set_errno(value) __percpu_set_u32(offsetof(per_cpu_t, errno), value)
#define __percpu_set_rflags(value) __percpu_set_u64(offsetof(per_cpu_t, rflags), value)
#define __percpu_set_address_space(value) __percpu_set_u64(offsetof(per_cpu_t, address_space), (uintptr_t) value)
#define __percpu_set_scheduler(value) __percpu_set_u64(offsetof(per_cpu_t, scheduler), (uintptr_t) value)
#define __percpu_set_cpu_info(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_info), (uintptr_t) value)
#define __percpu_set_cpu_gdt(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_gdt), (void *) value)
#define __percpu_set_cpu_idt(value) __percpu_set_u64(offsetof(per_cpu_t, cpu_idt), (void *) value)

#define __percpu_struct_ptr() ((per_cpu_t *) __percpu_get_self())
#define __percpu_field_ptr(field) (&__percpu_struct_ptr()->field)

#define PERCPU_ID __percpu_get_id()
#define PERCPU_THREAD __percpu_get_thread()
#define PERCPU_PROCESS __percpu_get_process()
#define PERCPU_RFLAGS __percpu_get_rflags()
#define PERCPU_ADDRESS_SPACE __percpu_get_address_space()
#define PERCPU_SCHEDULER __percpu_get_scheduler()
#define PERCPU_CPU_INFO __percpu_get_cpu_info()

#define PERCPU_SET_RFLAGS(value) __percpu_set_rflags(value)
#define PERCPU_SET_ADDRESS_SPACE(value) __percpu_set_address_space(value)
#define PERCPU_SET_SCHEDULER(value) __percpu_set_scheduler(value)
#define PERCPU_SET_CPU_INFO(value) __percpu_set_cpu_info(value)
#define PERCPU_SET_CPU_GDT(value) __percpu_set_cpu_gdt(value)
#define PERCPU_SET_CPU_IDT(value) __percpu_set_cpu_idt(value)

#ifndef __PER_CPU_BASE__

#endif

#endif
