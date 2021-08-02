%include "base.inc"
extern handle_syscall

global syscall_handler:
syscall_handler:
  push rbp
  mov rbp, rsp
  push rbx

  mov rdi, rax ; rax -> rdi
  mov rsi, rbx ; rbx -> rsi
  mov rax, rdx ; rdx -> rax (temp)
  mov rdx, rcx ; rcx -> rdx
  mov rcx, rax ; rdx -> rax -> rcx
  ; r8 -> r8
  ; r9 -> r9
  push r10

  call handle_syscall

  pop rbx
  pop rbp
  sysret

