%include "base.inc"

extern kmain
extern ap_main

global entry
entry:
  ; switch to new stack
  mov rsp, entry_initial_stack_top

  ; setup the bsp percpu structure
  mov rax, entry_initial_cpu_reserved   ; PERCPU area
  mov [rax + PERCPU_SELF], rax          ; PERCPU->self = rax
  mov qword [rax + PERCPU_ID], 0        ; PERCPU->id = 0
  mov rdx, rax
  mov cl, 32
  shr rdx, cl
  mov ecx, GS_BASE_MSR
  wrmsr

  ; percpu is now ok to use
  ;pop rdi
  cld
  cli

  call kmain   ; call the kernel
.hang:
  hlt
  jmp .hang    ; hang

global ap_entry
ap_entry:
;  jmp $
  ; switch to the new stack
  mov rsp, rsi ; stack pointer
  mov rbp, rsp

  cld
  cli
  call ap_main ; call the kernel
.hang:
  hlt
  jmp .hang    ; hang


section .data
align 0x1000
entry_initial_stack:
  resb PAGE_SIZE
entry_initial_stack_top:

entry_initial_cpu_reserved:
  resb PAGE_SIZE
