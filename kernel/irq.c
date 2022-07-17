//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <irq.h>

#include <cpu/idt.h>

#include <device/apic.h>
#include <device/ioapic.h>

#include <bus/pcie.h>

#include <process.h>
#include <spinlock.h>
#include <bitmap.h>
#include <printf.h>
#include <panic.h>

#define IRQ_NUM_VECTORS 256
#define IRQ_NUM_ISA     16
#define IRQ_VECTOR_BASE 32

#define IRQ_TYPE_FUNC 0x1
#define IRQ_TYPE_COND 0x2

struct irq_handler {
  uint8_t ignored;
  uint8_t type;
  union {
    void *ptr;
    irq_handler_t handler;
    cond_t *condition;
  };
  void *data;
};

struct isa_irq_override {
  uint8_t used;
  uint8_t dest_irq;
  uint16_t flags;
};

static int irq_external_max;
static spinlock_t irqnum_hardware_lock;
static bitmap_t *irqnum_hardware_map;
static spinlock_t irqnum_software_lock;
static bitmap_t *irqnum_software_map;

struct irq_handler irq_handlers[IRQ_NUM_VECTORS];
struct isa_irq_override irq_isa_overrides[IRQ_NUM_ISA];


__used void irq_handler(uint8_t vector) {
  apic_send_eoi();
  if (irq_handlers[vector].ptr != NULL) {
    if (irq_handlers[vector].ignored) {
      return;
    }

    switch (irq_handlers[vector].type) {
      case IRQ_TYPE_FUNC:
        irq_handlers[vector].handler(vector - IRQ_VECTOR_BASE, irq_handlers[vector].data);
        return;
      case IRQ_TYPE_COND:
        cond_signal(irq_handlers[vector].condition);
        return;
    }
  }

  kprintf("--> IRQ%d\n", vector - IRQ_VECTOR_BASE);
}

__used void exception_handler(uint8_t vector, uint32_t error, cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  apic_send_eoi();
  exception_handler_t handler = (void *) irq_handlers[vector].handler;
  if (handler) {
    handler(vector, error, frame, regs);
    return;
  }


  kprintf("!!! EXCEPTION %d !!!\n", vector);
  if (vector != CPU_EXCEPTION_DF) {
    kprintf("  CPU#%d - %#b\n", PERCPU_ID, error);
    kprintf("  RIP = %018p  RSP = %018p\n", frame->rip, frame->rip);
  }

  while (true) {
    cpu_pause();
  }
}

uint8_t irq_internal_map_to_vector(uint8_t irq) {
  uint8_t vector = irq + IRQ_VECTOR_BASE;
  if (irq <= irq_external_max) {
    // this is an external interrupt
    if (irq < IRQ_NUM_ISA && irq_isa_overrides[irq].used) {
      // isa interrupt with override
      vector = irq_isa_overrides[irq].dest_irq + IRQ_VECTOR_BASE;
      uint16_t flags = irq_isa_overrides[irq].flags;
      ioapic_set_isa_irq_routing(irq, vector, flags);
    } else {
      ioapic_set_irq_vector(irq, irq + IRQ_VECTOR_BASE);
    }
  }
  return vector;
}

//

void irq_early_init() {
  irqnum_hardware_map = create_bitmap(IRQ_NUM_VECTORS);
  irqnum_software_map = create_bitmap(IRQ_NUM_VECTORS);

  // mark all over the max number of interrupts as reserved
  bitmap_set_n(irqnum_hardware_map, IRQ_NUM_VECTORS - IRQ_VECTOR_BASE, IRQ_VECTOR_BASE);
  bitmap_set_n(irqnum_software_map, IRQ_NUM_VECTORS - IRQ_VECTOR_BASE, IRQ_VECTOR_BASE);

  // mark ISA interrupt numbers as reserved
  bitmap_set_n(irqnum_hardware_map, 0, IRQ_NUM_ISA);

  spin_init(&irqnum_hardware_lock);
  spin_init(&irqnum_software_lock);
}

void irq_init() {
  irq_external_max = ioapic_get_max_remappable_irq();

  // mask out all but the max number of hardware irqs
  bitmap_set_n(irqnum_hardware_map, irq_external_max + 1, IRQ_NUM_VECTORS - IRQ_VECTOR_BASE - irq_external_max);

  // set up the isa ioapic entries
  for (int irq = 0; irq < IRQ_NUM_ISA; irq++) {
    if (irq_isa_overrides[irq].used) {
      // isa interrupt with override
      uint8_t vector = irq_isa_overrides[irq].dest_irq + IRQ_VECTOR_BASE;
      uint16_t flags = irq_isa_overrides[irq].flags;
      ioapic_set_isa_irq_routing(irq, vector, flags);
    } else {
      ioapic_set_irq_vector(irq, irq + IRQ_VECTOR_BASE);
    }
  }
}

//

int irq_alloc_hardware_irqnum() {
  spin_lock(&irqnum_hardware_lock);
  index_t num = bitmap_get_set_free(irqnum_hardware_map);
  spin_unlock(&irqnum_hardware_lock);
  if (num < 0) {
    return -1;
  }
  return (uint8_t) num;
}

int irq_alloc_software_irqnum() {
  spin_lock(&irqnum_software_lock);
  index_t num = bitmap_get_set_free(irqnum_software_map);
  spin_unlock(&irqnum_software_lock);
  if (num < 0) {
    return -1;
  }
  return (uint8_t) num + irq_external_max + 1;
}

void irq_reserve_irqnum(uint8_t irq) {
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    panic("irq: invalid irq number");
  }

  bool pre_claimed;
  if (irq <= irq_external_max) {
    spin_lock(&irqnum_hardware_lock);
    pre_claimed = bitmap_set(irqnum_hardware_map, irq);
    spin_unlock(&irqnum_hardware_lock);
  } else {
    spin_lock(&irqnum_software_lock);
    pre_claimed = bitmap_set(irqnum_software_map, irq);
    spin_unlock(&irqnum_software_lock);
  }

  if (pre_claimed) {
    panic("irq: irq number %d already in-use", irq);
  }
}

//

int irq_register_exception_handler(uint8_t exception, exception_handler_t handler) {
  if (exception > CPU_MAX_EXCEPTION) {
    return -ERANGE;
  }

  irq_handlers[exception].ignored = 0;
  irq_handlers[exception].type = IRQ_TYPE_FUNC;
  irq_handlers[exception].handler = (void *) handler; // forgive me
  irq_handlers[exception].data = NULL;
  return 0;
}

int irq_register_irq_handler(uint8_t irq, irq_handler_t handler, void *data) {
  kprintf("irq: registering handler for IRQ%d\n", irq);
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  uint8_t vector = irq_internal_map_to_vector(irq);
  irq_handlers[vector].ignored = 1;
  irq_handlers[vector].type = IRQ_TYPE_FUNC;
  irq_handlers[vector].handler = handler;
  irq_handlers[vector].data = data;
  return 0;
}

int irq_register_signaled_irq_handler(uint8_t irq, cond_t *condition) {
  kprintf("irq: registering deferred handler for IRQ%d\n", irq);
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  uint8_t vector = irq_internal_map_to_vector(irq);
  irq_handlers[vector].ignored = 1;
  irq_handlers[vector].type = IRQ_TYPE_COND;
  irq_handlers[vector].condition = condition;
  irq_handlers[vector].data = NULL;
  return 0;
}

//

int irq_enable_interrupt(uint8_t irq) {
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  uint8_t vector = irq + IRQ_VECTOR_BASE;
  irq_handlers[vector].ignored = 0;
  if (irq <= irq_external_max) {
    ioapic_set_irq_mask(irq, 0);
  }
  return 0;
}

int irq_disable_interrupt(uint8_t irq) {
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  uint8_t vector = irq + IRQ_VECTOR_BASE;
  irq_handlers[vector].ignored = 1;
  if (irq <= irq_external_max) {
    ioapic_set_irq_mask(irq, 1);
  }
  return 0;
}

int irq_enable_msi_interrupt(uint8_t irq, uint8_t index, pcie_device_t *device) {
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  int result = irq_enable_interrupt(irq);
  if (result < 0) {
    return result;
  }

  uint8_t vector = irq + IRQ_VECTOR_BASE;
  pcie_enable_msi_vector(device, index, vector);
  return 0;
}

int irq_disable_msi_interrupt(uint8_t irq, uint8_t index, pcie_device_t *device) {
  if (irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  int result = irq_disable_interrupt(irq);
  if (result < 0) {
    return result;
  }

  pcie_disable_msi_vector(device, index);
  return 0;
}

//

int irq_override_isa_interrupt(uint8_t isa_irq, uint8_t dest_irq, uint16_t flags) {
  if (isa_irq > IRQ_NUM_ISA || dest_irq > IRQ_NUM_VECTORS - IRQ_VECTOR_BASE) {
    return -ERANGE;
  }

  irq_isa_overrides[isa_irq].used = 1;
  irq_isa_overrides[isa_irq].dest_irq = dest_irq;
  irq_isa_overrides[isa_irq].flags = flags;

  if (isa_irq != dest_irq && dest_irq < IRQ_NUM_ISA && !irq_isa_overrides[dest_irq].used) {
    // override the dest irq to point to the src irq
    irq_isa_overrides[dest_irq].used = 1;
    irq_isa_overrides[dest_irq].dest_irq = isa_irq;
    irq_isa_overrides[dest_irq].flags = 0;
  }

  return 0;
}
