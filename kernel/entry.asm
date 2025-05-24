extern kmain
extern ap_main

%define PAGE_SIZE 0x1000
%define GS_BASE_MSR 0xC0000101

%define PERCPU_ID_OFF         0x00
%define PERCPU_SELF_OFF       0x08

%define TCB_SIZE        0xa0 ; 0x98 aligned to 16 bytes
%define TRAPFRAME_SIZE  0xc0

; top of the thread stack sits below the trapframe and tcb
%define STACK_TOP_OFF   (TCB_SIZE + TRAPFRAME_SIZE)


; =======================
;       Kernel Entry
; =======================
global entry
entry:
  ; switch to new stack
  mov rsp, entry_initial_stack_top
  ; make space for the proc0 tcb and trapframe
  sub rsp, STACK_TOP_OFF

  ; setup the bsp percpu structure
  mov rax, entry_initial_cpu_reserved   ; PERCPU area
  mov dword [rax + PERCPU_ID_OFF], 0    ; PERCPU->id = 0
  mov qword [rax + PERCPU_SELF_OFF], rax; PERCPU->self = rax
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


; =======================
;        AP Entry
; =======================
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


; =======================
;         Data
; =======================
section .data
align 0x1000
global entry_initial_stack
global entry_initial_stack_top

entry_initial_stack:
  resb 4*PAGE_SIZE
entry_initial_stack_top:

entry_initial_cpu_reserved:
  resb PAGE_SIZE ; cpu0 percpu area
