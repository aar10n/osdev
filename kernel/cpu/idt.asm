;
; Interrupt Handling Code
;

%include "base.inc"

extern irq_handler
extern exception_handler

extern schedl_schedule
extern schedl_cleanup

%define IDT_STUB_SIZE 32
%define IDT_GATES 256

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
  pushall
  cld

  mov rdi, [rsp + 72]
  call irq_handler

  popall
  add rsp, 8
  swapgs_if_needed
  iretq

global ignore_irq
ignore_irq:
  iretq

; ---------------------- ;
;   Context Switching    ;
; ---------------------- ;

global context_switch
context_switch:
  swapgs_if_needed
  pushall

  call schedl_schedule      ; get the next process
  mov rbx, rax              ; new process
  mov rdx, current          ; old process

  cmp rdx, 0                ; check if old process is null
  jz .2                     ; if so skip saving context
  cmp rbx, rdx              ; check if process didn't change
  je .3                     ; if so skip to the end

  ; save old context
  mov rsi, rsp              ; context stack
  mov rdi, [rdx + PROC_CTX] ; context pointer

  xor ecx, ecx              ; register offset
.1:                         ; save context loop
  pop qword [rdi + rcx * 8] ; save irq pushed register
  inc ecx
  cmp ecx, 20
  jl .1

  mov rdi, current
  call schedl_cleanup       ; cleanup the old process

.2:
  and rbx, rbx              ; check if new process is null
  jz .3                     ; if so skip to end

  mov current, rbx          ; current = next
  mov rsp, [rbx + PROC_CTX] ; restore from process stack frame

.3:
  popall
  swapgs_if_needed

  ; send apic eoi
  mov dword [APIC_BASE_VA + APIC_REG_EOI], 0
  iretq
