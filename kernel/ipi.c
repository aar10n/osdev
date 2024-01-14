//
// Created by Aaron Gill-Braun on 2022-07-20.
//

#include <kernel/ipi.h>
#include <kernel/irq.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/mm.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#include <kernel/hw/apic.h>

uint8_t ipi_irqnum;
uint8_t ipi_vectornum;
static mtx_t ipi_lock;
static uint8_t ipi_type;
static uint64_t ipi_data;
static uint8_t ipi_ack;

__used void ipi_handler(struct trapframe *frame) {
  uint8_t type = ipi_type;
  uint64_t data = ipi_data;
  kassert(type < NUM_IPIS);
  atomic_fetch_add(&ipi_ack, 1);

  // kprintf("[CPU#%d] ipi %d\n", curcpu_id, type);
  switch (type) {
    case IPI_PANIC:
      if (data != 0) {
        if (!is_kernel_code_ptr(data)) {
          kprintf("CPU#%d IPI panic - bad handler!\n", curcpu_id);
        } else {
          ((irq_handler_t)((void *) data))(frame);
        }
      }
      WHILE_TRUE;
      unreachable;
    case IPI_INVLPG:
      todo("IPI_INVLPG");
      break;
    case IPI_SCHEDULE:
      todo("reschedule");
      break;
    case IPI_NOOP:
      break;
    default: unreachable;
  }
}

static void ipi_static_init() {
  mtx_init(&ipi_lock, MTX_SPIN, "ipi_lock");
  ipi_irqnum = irq_must_reserve_irqnum(MAX_IRQ-1);
  ipi_vectornum = (uint8_t) irq_get_vector(ipi_vectornum);

  if (irq_register_handler(ipi_irqnum, ipi_handler, NULL) < 0) {
    panic("failed to register ipi handler");
  }
}
STATIC_INIT(ipi_static_init);

//

int ipi_deliver_cpu_id(ipi_type_t type, uint8_t cpu_id, uint64_t data) {
  kassert(type < NUM_IPIS);
  if (cpu_id > system_num_cpus) {
    return -1;
  }

  // kprintf("[CPU#%d] delivering ipi to CPU#%d\n", PERCPU_ID, cpu_id);

  // we cant use spin_lock here because it disables interrupts
  while (!mtx_spin_trylock(&ipi_lock)) {
    cpu_pause();
  }
  ipi_type = type;
  ipi_data = data;
  ipi_ack = 0;

  apic_write_icr(APIC_DM_FIXED | APIC_LVL_ASSERT | ipi_vectornum, cpu_id);

  // cpu_enable_interrupts();
  // while (*((volatile uint8_t *)(&ipi_ack)) != 1) {
  //   cpu_pause();
  // }
  // cpu_disable_interrupts();

  mtx_spin_unlock(&ipi_lock);
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

  mtx_spin_lock(&ipi_lock);
  ipi_type = type;
  ipi_data = data;
  ipi_ack = 0;
  apic_write_icr(APIC_DM_FIXED | APIC_LVL_ASSERT | apic_flags | ipi_vectornum, 0);
  while (ipi_ack != num_acks) {
    cpu_pause();
  }
  mtx_spin_unlock(&ipi_lock);
  return 0;
}
