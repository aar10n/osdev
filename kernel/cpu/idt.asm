;
; Interrupt Handling Code
;

%include "base.inc"

extern irq_handler
extern exception_handler

%define KERNEL_OFFSET 0xFFFFFF8000000000
%define IDT_STUB_SIZE 32
%define IDT_GATES 256

%define APIC_BASE_VA 0xFFFFFFFFF8000000
%define APIC_REG_ID  0x20
%define APIC_REG_EOI 0x0B0

%macro pushall 0
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

%macro popall 0
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
    call interrupt_handler
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
  pushall
  cld

  mov byte edi, [rsp]
  call irq_handler

  popall
  add rsp, 8
  iretq

global ignore_irq
ignore_irq:
  iretq
