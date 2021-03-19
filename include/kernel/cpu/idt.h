//
// Created by Aaron Gill-Braun on 2019-04-18.
//

#ifndef KERNEL_CPU_IDT_H
#define KERNEL_CPU_IDT_H

#include <base.h>
#include <cpu/cpu.h>
#include <process.h>

#define IDT (PERCPU->idt->idt)
#define IDT_DESC (PERCPU->idt->desc)
#define IDT_HANDLERS (PERCPU->idt->handlers)

#define IDT_GATES 256
#define IDT_STUB_SIZE 32

// IDT Gate Types
#define CALL_GAGE 0xC
#define INTERRUPT_GATE 0xE
#define TRAP_GATE 0xF

#define gate(offset, selector, ist, type, dpl, p)   \
  ((idt_gate_t) {                                   \
    (offset) & 0xFFFF, selector, ist, type, dpl, p, \
    (offset) >> 16, (offset) >> 32                  \
  })

typedef struct packed {
  uint64_t low_offset : 16;  // low 16 bits of the isr address
  uint64_t selector : 16;    // segment selector for dest code segment
  uint64_t ist : 3;          // interrupt stack table
  uint64_t : 5;              // reserved
  uint64_t type : 4;         // gate type
  uint64_t : 1;              // reserved
  uint64_t dpl : 2;          // descriptor privilege level
  uint64_t present : 1;      // segment present
  uint64_t mid_offset : 16;  // mid 16 bits of the isr address
  uint64_t high_offset : 32; // high 32 bits of the isr address
  uint64_t : 32;             // reserved
} idt_gate_t;
static_assert(sizeof(idt_gate_t) == (sizeof(uint64_t) * 2));

typedef struct packed {
  union {
    uint64_t rax;
    struct {
      uint32_t     : 32;
      uint32_t eax : 32;
    };
  };
  union {
    uint64_t rcx;
    struct {
      uint32_t     : 32;
      uint32_t ecx : 32;
    };
  };
  union {
    uint64_t rdx;
    struct {
      uint32_t     : 32;
      uint32_t edx : 32;
    };
  };
  union {
    uint64_t rdi;
    struct {
      uint32_t     : 32;
      uint32_t edi : 32;
    };
  };
  union {
    uint64_t rsi;
    struct {
      uint32_t     : 32;
      uint32_t esi : 32;
    };
  };
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
} regs_t;
static_assert(sizeof(regs_t) == (sizeof(uint64_t) * 9));

typedef void (*idt_function_t)(uint8_t, void *);

typedef struct {
  idt_function_t fn;
  void *data;
} idt_handler_t;

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} idt_desc_t;

typedef struct idt {
  idt_gate_t idt[IDT_GATES];
  idt_handler_t handlers[IDT_GATES];
  idt_desc_t desc;
} idt_t;

void setup_idt();
void idt_set_gate(uint8_t vector, idt_gate_t gate);
void idt_hook(uint8_t vector, idt_function_t fn, void *data);
void *idt_unhook(uint8_t vector);

#endif // KERNEL_CPU_IDT_H
