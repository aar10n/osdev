;
; Interrupt Handling
;

;%include "base.inc"

extern irq_handler
extern exception_handler

%define NULL 0
%define KERNEL_OFFSET 0xFFFFFF8000000000
%define KERNEL_STACK  0xFFFFFFA000000000
%define KERNEL_CS 0x08
%define SMPBOOT_START 0x0000
%define SMPDATA_START 0x1000

%define APIC_BASE     0xFEE00000
%define APIC_BASE_VA  0xFFFFFFFFE0000000
%define APIC_REG_ID   0x20
%define APIC_REG_EOI  0x0B0

%define IDT_STUB_SIZE 32
%define IDT_GATES 256

%define PUSHALL_COUNT     15
%define STACK_OFFSET(x) ((x) * 8)

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

%macro swapgs_if_needed 1
  cmp word [rsp + %1], KERNEL_CS
  je %%skip
  swapgs
  %%skip:
%endmacro

; The follow stub is used for both exceptions
; and regular interrupts. In the case of IRQs
; and exceptions which do not have an error code,
; 0 is pushed instead.
%macro interrupt_stub 1
  %%start:
  %if %1 < 32
    %if (1 << %1) & 0x27D00 == 0
      push qword 0
    %endif
    push qword %1
;    jmp exception_handler
    jmp __exception_handler
  %else
    push qword %1
    jmp __interrupt_handler
  %endif
  times IDT_STUB_SIZE - ($ - %%start) db 0
%endmacro

; interrupt handler stubs that push the
; vector number onto the stack, and add
; a dummy value if no error code exists
global idt_stubs
idt_stubs:
  %assign i 0
  %rep 256
    interrupt_stub i
  %assign i i +  1
  %endrep

; ---------------------- ;
;   Interrupt Handling   ;
; ---------------------- ;

__interrupt_handler:
  ;        stack frame
  ; rsp -> vector
  swapgs_if_needed 16
  pushall

  mov rdi, [rsp + STACK_OFFSET(PUSHALL_COUNT)] ; rdi <- vector

  cld
  call irq_handler

  popall
  swapgs_if_needed 16

  add rsp, 8 ; skip vector
  iretq

; ---------------------- ;
;   Exception Handling   ;
; ---------------------- ;

__exception_handler:
  ;        stack frame
  ;        error code
  ; rsp -> vector
  swapgs_if_needed 24
  pushall

  mov rdi, [rsp + STACK_OFFSET(PUSHALL_COUNT)]           ; rdi <- vector
  mov dword rsi, [rsp + STACK_OFFSET(PUSHALL_COUNT + 1)] ; rdi <- error code
  mov rdx, rsp
  add rdx, STACK_OFFSET(PUSHALL_COUNT + 2)         ; rdx <- interrupt stack frame pointer
  mov rcx, rsp                                     ; rcx <- registers pointer

  cld
  call exception_handler

  popall
  swapgs_if_needed 24

  add rsp, 16 ; skip vector + error code
  iretq
