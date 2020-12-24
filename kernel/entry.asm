%include "base.inc"

extern kmain
extern ap_main

global entry
entry:
  ; switch to the new stack
  mov rsp, rdi ; stack pointer
  xor rbp, rbp

  mov rdi, rsi ; boot_info pointer

  mov ecx, IA32_EFER_MSR
  rdmsr
  mov edx, (1 << 11)
  not edx

  or eax, 1
  and eax, edx
  wrmsr

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
