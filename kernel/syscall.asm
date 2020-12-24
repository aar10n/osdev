extern handle_syscall

global syscall_handler:
syscall_handler:
  push rbp
  mov rbp, rsp
  push rcx

  mov rdi, rax
  mov rsi, rbx
  mov r8,  rdx
  mov rdx, rcx
  call handle_syscall

  pop rcx
  pop rbp
  sysret

