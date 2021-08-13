%include "base.inc"
extern handle_syscall

global syscall_handler:
syscall_handler:
  push rbp
  mov rbp, rsp
  push rcx
  push r11

  mov r11, rdi ; preserve rdip
  mov rdi, rax ; rax -> rdi (call)
  mov rax, rsi ; preserve rsi
  mov rsi, r11 ; rdi -> rsi (arg1)
  mov r11, rdx ; preserve rdx
  mov rdx, rax ; rsi -> rdx (arg2)
  mov rcx, r11 ; rdx -> rcx (arg3)
               ; r8  -> r8 (arg4)
               ; r9  -> r9 (arg5)
  push r10     ; r10 -> stack (arg6)

  call handle_syscall

  pop r10
  pop r11
  pop rcx
  pop rbp
  o64 sysret

