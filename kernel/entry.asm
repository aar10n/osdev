%include "kernel/base.inc"

extern kmain
extern ap_main

global entry
entry:
  ; determine APIC id
  mov eax, 0x1
  cpuid
  mov cl, 24
  shr ebx, cl
  and ebx, 0xFF

  ; switch to new stack
  mov rsp, entry_initial_stack_top

  ; setup the bsp percpu structure
  mov rax, entry_initial_cpu_reserved   ; PERCPU area
  mov [rax + PERCPU_SELF], rax          ; PERCPU->self = rax
  mov word [rax + PERCPU_ID], 0         ; PERCPU->id = 0
  mov word [rax + PERCPU_APIC_ID], bx   ; PERCPU->apic_id = bx
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
  mov rbp, rsp
  push rbp

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
global entry_initial_stack_top
entry_initial_stack_top:

entry_initial_cpu_reserved:
  resb PAGE_SIZE
