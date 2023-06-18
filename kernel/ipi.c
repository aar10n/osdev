//
// Created by Aaron Gill-Braun on 2022-07-20.
//

#include <kernel/ipi.h>

#include <kernel/device/apic.h>

#include <kernel/sched.h>
#include <kernel/irq.h>
#include <kernel/mm.h>

#include <kernel/spinlock.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <atomic.h>

#include <kernel/cpu/io.h>

static uint8_t ipi_type;
static uint64_t ipi_data;
static spinlock_t ipi_lock;
static uint8_t ipi_ack;

typedef void (*panic_fn_t)(cpu_irq_stack_t *frame, cpu_registers_t *regs);

#define QDEBUG_VALUE(v) ({ outdw(0x800, v); })
#define QDEBUG_PRINT(str) \
  ({                      \
    const char *_ptr = str; \
    while (*_ptr) {       \
      outb(0x810 + PERCPU_ID, *_ptr); \
      _ptr++; \
    }                     \
    outb(0x810 + PERCPU_ID, '\0'); \
  })


__used void ipi_handler(cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  uint8_t type = ipi_type;
  uint64_t data = ipi_data;
  kassert(type < NUM_IPIS);
  atomic_fetch_add(&ipi_ack, 1);
  QDEBUG_PRINT("RECEIVED IPI");

  // kprintf("[CPU#%d] ipi %d\n", PERCPU_ID, type);
  switch (type) {
    case IPI_PANIC:
      if (data != 0) {
        if (!mm_is_kernel_code_ptr(data)) {
          kprintf("CPU#%d IPI panic - bad handler!\n", PERCPU_ID);
          while (true) cpu_pause();
        }

        ((panic_fn_t)((void *) data))(frame, regs);
        while (true) cpu_pause();
      }
      unreachable;
    case IPI_INVLPG:
      kassert(false && "not implemented");
      unreachable;
    case IPI_SCHEDULE:
      sched_reschedule((sched_cause_t) data);
      break;
    case IPI_NOOP:
      break;
    default: unreachable;
  }
}

//

int ipi_deliver_cpu_id(ipi_type_t type, uint8_t cpu_id, uint64_t data) {
  kassert(type < NUM_IPIS);
  if (cpu_id > system_num_cpus) {
    return -1;
  }

  // kprintf("[CPU#%d] delivering ipi to CPU#%d\n", PERCPU_ID, cpu_id);

  // we cant use spin_lock here because it disables interrupts
  while (!spin_trylock(&ipi_lock)) {
    cpu_pause();
  }
  ipi_type = type;
  ipi_data = data;
  ipi_ack = 0;

  QDEBUG_PRINT("SENDING IPI");
  apic_write_icr(APIC_DM_FIXED | APIC_LVL_ASSERT | ipi_vectornum, cpu_id);

  // cpu_enable_interrupts();
  // while (*((volatile uint8_t *)(&ipi_ack)) != 1) {
  //   cpu_pause();
  // }
  // cpu_disable_interrupts();

  spin_unlock(&ipi_lock);
  return 0;
}

int ipi_deliver_mode(ipi_type_t type, ipi_mode_t mode, uint64_t data) {
  kassert(type < NUM_IPIS);
  kprintf("[CPU#%d] delivering ipi using mode %d\n", PERCPU_ID, mode);

  uint32_t apic_flags;
  uint32_t num_acks;
  switch (mode) {
    case IPI_SELF:
      apic_flags = APIC_DS_SELF;
      num_acks = 1;
      break;
    case IPI_ALL_INCL:
      apic_flags = APIC_DS_ALLINC;
      num_acks = system_num_cpus;
      break;
    case IPI_ALL_EXCL:
      apic_flags = APIC_DS_ALLBUT;
      num_acks = system_num_cpus - 1;
      break;
    default:
      panic("invalid ipi mode");
  }

  spin_lock(&ipi_lock);
  ipi_type = type;
  ipi_data = data;
  ipi_ack = 0;
  apic_write_icr(APIC_DM_FIXED | APIC_LVL_ASSERT | apic_flags | ipi_vectornum, 0);
  while (ipi_ack != num_acks) {
    cpu_pause();
  }
  spin_unlock(&ipi_lock);
  return 0;
}
