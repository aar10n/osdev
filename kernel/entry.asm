extern main

global entry
entry:
  ; switch to the new stack
  mov rsp, rdi ; stack pointer
  xor rbp, rbp ; null stack frame

  mov rdi, rsi ; boot_info pointer
  call main    ; call the kernel
.hang:
  hlt
  jmp .hang    ; hang
