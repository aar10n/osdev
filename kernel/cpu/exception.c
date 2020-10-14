//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <cpu/exception.h>
#include <stdio.h>

static const char *get_exception(uint8_t exc) {
  switch (exc) {
    case EXC_DE:
      return "Division By Zero";
    case EXC_DB:
      return "Debug";
    case EXC_NMI:
      return "Non Maskable Interrupt";
    case EXC_BP:
      return "Breakpoint";
    case EXC_OF:
      return "Overflow";
    case EXC_BR:
      return "Out of Bounds";
    case EXC_UD:
      return "Invalid Opcode";
    case EXC_NM:
      return "No Coprocessor";
    case EXC_DF:
      return "Double Fault";
    case EXC_CSO:
      return "Coprocessor Segment Overrun";
    case EXC_TS:
      return "Bad TSS";
    case EXC_NP:
      return "Segment Not Present";
    case EXC_SS:
      return "Stack Fault";
    case EXC_GP:
      return "General Protection Fault";
    case EXC_PF:
      return "Page Fault";
    case EXC_MF:
      return "x87 Floating-Point Error";
    case EXC_AC:
      return "Alignment Check";
    case EXC_MC:
      return "Machine Check";
    case EXC_XM:
      return "SIMD Floating-Point Error";
    case EXC_VE:
      return "Virtualization Exception";
    case EXC_CP:
      return "Control Protection Exception";
    default:
      return "Unknown Exception";
  }
}

noreturn void exception_handler(cpu_state_t *state) {
  kprintf("!!!! Exception Type - %s !!!!\n", get_exception(state->int_no));
  kprintf("CPU Id: %d | Exception Code: %d | Exception Data: %#b\n",
          state->apic_id, state->int_no, state->err_code);
  kprintf("RIP = %016X, RFLAGS = %016X\n"
          "CS  = %016X, SS  = %016X\n",
          state->rip, state->rflags, state->cs, state->ss);
  kprintf("RAX = %016X, RBX = %016X, RCX = %016X\n"
          "RDX = %016X, RSP = %016X, RBP = %016X\n",
          state->rax, state->rbx, state->rcx, state->rdx,
          state->rsp, state->rbp);
  kprintf("RDI = %016X, RSI = %016X\n",
          state->rdi, state->rsi);
  kprintf("R8  = %016X, R9  = %016X, R12 = %016X\n"
          "R13 = %016X, R14 = %016X, R15 = %016X\n",
          state->r8, state->r9, state->r12, state->r13,
          state->r14, state->r15);
  kprintf("CR0 = %016X, CR2 = %016X, CR3 = %016X\n"
          "CR4 = %016X\n",
          state->cr0, state->cr2, state->cr3, state->cr4);

  // hang
  while (true) {
    asm("hlt");
  }
}
