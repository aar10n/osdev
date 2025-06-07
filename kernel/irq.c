//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <kernel/irq.h>
#include <kernel/proc.h>
#include <kernel/mutex.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <kernel/hw/apic.h>
#include <kernel/hw/ioapic.h>

#include <kernel/bus/pci.h>

#include <bitmap.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("irq: " x, ##__VA_ARGS__)

// this code deals with two different concepts: interrupt vectors and IRQ numbers.
//
// interrupt vectors are indicies into the IDT that the CPU uses to dispatch the ISRs.
// there are 256 in total with the first 32 reserved for exceptions. this leaves 224
// vectors for us to map to IRQs. vector numbers are an implementation detail and are
// not exposed to other kernel code.
//
// IRQ numbers uniquely identify a interrupt sources and range from 0-224. these sources
// include external interrupts, internal interrupts, and software interrupts. IRQs 0-15
// have special meaning and are reserved for ISA interrupts. Any IOAPIC remappable IRQs
// above 15 are considered 'hardware' interrupts. IRQs above the max IOAPIC remappable
// IRQ are considered 'software' interrupts and are suitable for things like MSI(-X).

#define NUM_EXCEPTS     32
#define NUM_INTERRUPTS  224
#define NUM_VECTORS     256

#define NUM_ISA_IRQS    16
#define IRQ_TO_VECTOR(i) ((i) + NUM_EXCEPTS)

struct irq_handler {
  irq_handler_t handler;
  void *data;
};

struct isa_irq_override {
  uint8_t used;
  uint8_t dest_irq;
  uint16_t flags;
};

// both the hardware and software irqnum bitmaps cover the same range of irq numbers
// but each is restricted to a subset of the range with the remaining bits reserved
static int irq_external_max;
static bitmap_t *irqnums_hardware;
static bitmap_t *irqnums_software;
static mtx_t irqnums_lock;

static mtx_t handlers_lock;
static struct irq_handler handlers[NUM_VECTORS]; // handlers covers both exceptions and interrupts
static uint8_t ignored_irqs[NUM_INTERRUPTS]; // only interrupts can be ignored with the irq api

static struct isa_irq_override irq_isa_overrides[NUM_ISA_IRQS];


void page_fault_handler(struct trapframe *frame);

__used noreturn void double_fault_handler() {
  kprintf_kputs("!!! DOUBLE FAULT !!!\n");
  WHILE_TRUE;
}

__used void interrupt_handler(struct trapframe *frame) {
  if (ignored_irqs[frame->vector])
    return;

  struct irq_handler *handler = &handlers[frame->vector];
  frame->data = (uintptr_t) handler->data;
  handler->handler(frame);
  apic_send_eoi();
}

noreturn void default_exception_handler(struct trapframe *frame) {
  kprintf("!!! EXCEPTION %d !!!\n", frame->vector);
  kprintf("  CPU#%d - %#llx\n", curcpu_id, frame->error);
  kprintf("  RIP = %018p  RSP = %018p\n", frame->rip, frame->rsp);
  kprintf("  CR2 = %018p\n", __read_cr2());
  WHILE_TRUE;
}

void unhandled_interrupt_handler(struct trapframe *frame) {
  kprintf("!!! UNHANDLED INTERRUPT %d !!!\n", frame->vector);
  kprintf("  CPU#%d\n", curcpu_id);
}

//

// returns the 'real' irq number which should be used for indexing the handlers array.
// normally this just returns the irq number, except for ISA interrupts that have an
// active override.
static inline uint8_t get_real_irqnum(uint8_t irq) {
  if (irq < NUM_ISA_IRQS && irq_isa_overrides[irq].used) {
    return irq_isa_overrides[irq].dest_irq;
  }
  return irq;
}

// returns the vector for the given irq number. if the irq is an external interrupt
// this will setup the ioapic entry and account for any ISA overrides.
static inline uint8_t setup_irq_vector(uint8_t irq) {
  uint8_t vector = IRQ_TO_VECTOR(irq);
  if (irq <= irq_external_max) {
    // this is an external interrupt
    if (irq < NUM_ISA_IRQS && irq_isa_overrides[irq].used) {
      // isa interrupt with override
      irq = irq_isa_overrides[irq].dest_irq;
      vector = IRQ_TO_VECTOR(irq);
      ioapic_set_isa_irq_routing(irq, vector, irq_isa_overrides[irq].flags);
    } else {
      ioapic_set_irq_vector(irq, vector);
    }
  }
  return vector;
}

static inline void mask_irq(uint8_t irq) {
  irq = get_real_irqnum(irq);
  ignored_irqs[irq] = 1;
  if (irq <= irq_external_max) {
    ioapic_set_irq_mask(irq, 1);
  }
}

static inline void unmask_irq(uint8_t irq) {
  irq = get_real_irqnum(irq);
  ignored_irqs[irq] = 0;
  if (irq <= irq_external_max) {
    ioapic_set_irq_mask(irq, 0);
  }
}

//

void irq_init() {
  irq_external_max = ioapic_get_max_remappable_irq();
  irqnums_hardware = create_bitmap(NUM_INTERRUPTS);
  irqnums_software = create_bitmap(NUM_INTERRUPTS);
  mtx_init(&irqnums_lock, MTX_SPIN, "irqnums_lock");

  // the hardware bitmap should have the ISA and software irqnums reserved
  bitmap_set_n(irqnums_hardware, 0, NUM_ISA_IRQS);
  bitmap_set_n(irqnums_hardware, irq_external_max+1, NUM_INTERRUPTS-irq_external_max-1);
  // the software bitmap should have everything up to the max remappable irq reserved
  bitmap_set_n(irqnums_software, 0, irq_external_max+1);

  // set up the isa ioapic entries
  for (int irq = 0; irq < NUM_ISA_IRQS; irq++) {
    if (irq_isa_overrides[irq].used) {
      // isa interrupt with override
      uint8_t dest_irq = irq_isa_overrides[irq].dest_irq;
      uint16_t flags = irq_isa_overrides[irq].flags;
      ioapic_set_isa_irq_routing(irq, IRQ_TO_VECTOR(dest_irq), flags);
    } else {
      ioapic_set_irq_vector(irq, IRQ_TO_VECTOR(irq));
    }
  }

  // setup the default handlers
  mtx_init(&handlers_lock, MTX_SPIN, "handlers_lock");
  for (int i = 0; i < NUM_EXCEPTS; i++) {
    if (i == CPU_EXCEPTION_PF) {
      handlers[i].handler = page_fault_handler;
    } else {
      handlers[i].handler = default_exception_handler;
    }
    handlers[i].data = NULL;
  }
  for (int i = NUM_EXCEPTS; i < NUM_VECTORS; i++) {
    handlers[i].handler = unhandled_interrupt_handler;
    handlers[i].data = NULL;
  }
}

int irq_get_vector(uint8_t irq) {
  if (irq >= NUM_INTERRUPTS) {
     return -ERANGE;
  }
  return IRQ_TO_VECTOR(get_real_irqnum(irq));
}

// MARK: IRQ Numbers

int irq_alloc_hardware_irqnum() {
  mtx_spin_lock(&irqnums_lock);
  index_t irqnum = bitmap_get_set_free(irqnums_hardware);
  mtx_spin_unlock(&irqnums_lock);
  if (irqnum < 0) {
    return -1;
  }
  return (uint8_t) irqnum;
}

int irq_alloc_software_irqnum() {
  mtx_spin_lock(&irqnums_lock);
  index_t irqnum = bitmap_get_set_free(irqnums_software);
  mtx_spin_unlock(&irqnums_lock);
  if (irqnum < 0) {
    return -1;
  }
  return (uint8_t) irqnum;
}

int irq_try_reserve_irqnum(uint8_t irq) {
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  bool used;
  mtx_spin_lock(&irqnums_lock);
  if (irq <= irq_external_max) {
    used = bitmap_set(irqnums_hardware, irq);
  } else {
    used = bitmap_set(irqnums_software, irq);
  }
  mtx_spin_unlock(&irqnums_lock);

  if (used) {
    return -EADDRINUSE;
  }
  return irq;
}

int irq_must_reserve_irqnum(uint8_t irq) {
  ASSERT(irq < NUM_INTERRUPTS);
  if (irq < NUM_ISA_IRQS) {
    // already reserved to prevent allocation but not to prevent use
    return irq;
  }

  int result = irq_try_reserve_irqnum(irq);
  if (result < 0) {
    panic("irq: cannot reserve already in use IRQ%d", irq);
  }
  return result;
}

// MARK: IRQ Handlers

int irq_register_handler(uint8_t irq, irq_handler_t handler, void *data) {
  DPRINTF("registering handler for IRQ%d\n", irq);
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  mtx_spin_lock(&handlers_lock);
  uint8_t vector = setup_irq_vector(irq);
  handlers[vector].handler = handler;
  handlers[vector].data = data;
  mask_irq(irq); // disabled by default
  mtx_spin_unlock(&handlers_lock);
  return 0;
}

int irq_unregister_handler(uint8_t irq) {
  DPRINTF("unregistering handler for IRQ%d\n", irq);
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  mtx_spin_lock(&handlers_lock);
  uint8_t vector = IRQ_TO_VECTOR(get_real_irqnum(irq));
  handlers[vector].handler = unhandled_interrupt_handler;
  handlers[vector].data = NULL;
  unmask_irq(irq);
  mtx_spin_unlock(&handlers_lock);
  return 0;
}

int irq_enable_interrupt(uint8_t irq) {
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  mtx_spin_lock(&handlers_lock);
  unmask_irq(irq);
  mtx_spin_unlock(&handlers_lock);
  return 0;
}

int irq_disable_interrupt(uint8_t irq) {
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  mtx_spin_lock(&handlers_lock);
  mask_irq(irq);
  mtx_spin_unlock(&handlers_lock);
  return 0;
}

int irq_enable_msi_interrupt(uint8_t irq, uint8_t index, struct pci_device *device) {
  ASSERT(irq > irq_external_max);
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  int result;
  if ((result = irq_enable_interrupt(irq)) < 0) {
    return result;
  }

  uint8_t vector = IRQ_TO_VECTOR(get_real_irqnum(irq));
  pci_enable_msi_vector(device, index, vector);
  return 0;
}

int irq_disable_msi_interrupt(uint8_t irq, uint8_t index, struct pci_device *device) {
  ASSERT(irq > irq_external_max);
  if (irq >= NUM_INTERRUPTS) {
    return -ERANGE;
  }

  int result;
  if ((result = irq_disable_interrupt(irq)) < 0) {
    return result;
  }

  pci_disable_msi_vector(device, index);
  return 0;
}

//

// this is only called from acpi code early during boot
int early_irq_override_isa_interrupt(uint8_t isa_irq, uint8_t dest_irq, uint16_t flags) {
  if (isa_irq > NUM_ISA_IRQS || dest_irq > NUM_INTERRUPTS) {
    return -ERANGE;
  }

  irq_isa_overrides[isa_irq].used = 1;
  irq_isa_overrides[isa_irq].dest_irq = dest_irq;
  irq_isa_overrides[isa_irq].flags = flags;

  if (isa_irq != dest_irq && dest_irq < NUM_ISA_IRQS && !irq_isa_overrides[dest_irq].used) {
    // override the dest irq to point to the src irq
    irq_isa_overrides[dest_irq].used = 1;
    irq_isa_overrides[dest_irq].dest_irq = isa_irq;
    irq_isa_overrides[dest_irq].flags = 0;
  }

  return 0;
}
