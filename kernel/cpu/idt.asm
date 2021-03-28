;
; Interrupt Handling
;

%include "base.inc"

extern irq_handler
extern exception_handler
extern timer_handler

extern scheduler_tick

%define IDT_STUB_SIZE 32
%define IDT_GATES 256

%macro swapgs_if_needed 0
  cmp word [rsp + 8], KERNEL_CS
  je %%skip
  swapgs
  %%skip:
%endmacro

; The follow stub is used for both exceptions
; and regular interrupts. In the case of irq's
; and exceptions which do not have an error code,
; 0 is pushed instead.
%macro interrupt_stub 1
  %%start:
  %if %1 < 32
    %if (1 << %1) & 0x27D00 == 0
      push 0
    %endif
    push %1
    jmp exception_handler
  %else
    push %1
    jmp interrupt_handler
    iretq
  %endif
  times IDT_STUB_SIZE - ($ - %%start) db 0
%endmacro

; interrupt handler stubs that push the
; vector number onto the stack, and add
; a dummy value if no error code exists
global idt_stubs
idt_stubs:
  %assign i 0
  %rep 255
    interrupt_stub i
  %assign i i +  1
  %endrep

; ---------------------- ;
;   Interrupt Handling   ;
; ---------------------- ;

interrupt_handler:
  add rsp, 8
  swapgs_if_needed
  sub rsp, 8
  pushcaller
  cld

  mov rdi, [rsp + 72]
  mov rsi, rsp
  call irq_handler

  popcaller
  add rsp, 8
  swapgs_if_needed
  iretq

global ignore_irq
ignore_irq:
  iretq

global hpet_handler
hpet_handler:
  ; send apic eoi
  mov dword [APIC_BASE_VA + APIC_REG_EOI], 0
  pushall
  call timer_handler
  popall
  iretq

global tick_handler
tick_handler:
  mov dword [APIC_BASE_VA + APIC_REG_EOI], 0
  pushall
  call scheduler_tick
  popall
  iretq
