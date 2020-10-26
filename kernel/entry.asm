extern kmain
extern ap_main

global entry
entry:
  ; switch to the new stack
  mov rsp, rdi ; stack pointer
  xor rbp, rbp

  mov rdi, rsi ; boot_info pointer

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
