;
; Interrupt Handling
;

;%include "base.inc"

extern irq_handler
extern exception_handler
extern timer_handler

extern scheduler_tick

%define NULL 0
%define KERNEL_OFFSET 0xFFFFFF8000000000
%define KERNEL_STACK  0xFFFFFFA000000000
%define KERNEL_CS 0x08
%define SMPBOOT_START 0x0000
%define SMPDATA_START 0x1000

%define APIC_BASE_VA  0xFFFFFFFFE0000000
%define APIC_REG_ID   0x20
%define APIC_REG_EOI  0x0B0

%define IDT_STUB_SIZE 32
%define IDT_GATES 256

; Saved registers
%macro pushall 0
  push r15
  push r14
  push r13
  push r12
  push r11
  push r10
  push r9
  push r8
  push rbp
  push rsi
  push rdi
  push rdx
  push rcx
  push rbx
  push rax
%endmacro

%macro popall 0
  pop rax
  pop rbx
  pop rcx
  pop rdx
  pop rdi
  pop rsi
  pop rbp
  pop r8
  pop r9
  pop r10
  pop r11
  pop r12
  pop r13
  pop r14
  pop r15
%endmacro

%macro pushcaller 0
  push r11
  push r10
  push r9
  push r8
  push rsi
  push rdi
  push rdx
  push rcx
  push rax
%endmacro

%macro popcaller 0
  pop rax
  pop rcx
  pop rdx
  pop rdi
  pop rsi
  pop r8
  pop r9
  pop r10
  pop r11
%endmacro

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
