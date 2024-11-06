extern kmain
extern ap_main

%define PAGE_SIZE 0x1000
%define GS_BASE_MSR 0xC0000101

%define PERCPU_ID   0x00
%define PERCPU_SELF 0x08

global entry
entry:
  ; switch to new stack
  mov rsp, entry_initial_stack_top

  ; setup the bsp percpu structure
  mov rax, entry_initial_cpu_reserved   ; PERCPU area
  mov dword [rax + PERCPU_ID], 0        ; PERCPU->id = 0
  mov qword [rax + PERCPU_SELF], rax    ; PERCPU->self = rax
  mov rdx, rax
  mov cl, 32
  shr rdx, cl
  mov ecx, GS_BASE_MSR
  wrmsr

  ; percpu is now ok to use
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
global entry_initial_stack
entry_initial_stack:
  resb 4*PAGE_SIZE
global entry_initial_stack_top
entry_initial_stack_top:

entry_initial_cpu_reserved:
  resb PAGE_SIZE
