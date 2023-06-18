%include "kernel/base.inc"
extern handle_syscall

%macro swapgs_if_needed 1
  cmp word [rsp + %1], KERNEL_CS
  je %%skip
  swapgs
  %%skip:
%endmacro

global syscall_handler:
syscall_handler:
  swapgs_if_needed 0
  mov USER_SP, rsp
  mov rsp, KERNEL_SP

  ; syscall abi
  ;   rax -> syscall number
  ;   rdi -> arg1
  ;   rsi -> arg2
  ;   rdx -> arg3
  ;   r10 -> arg4
  ;   r8  -> arg5
  ;   r9  -> arg6

  push rbp
  mov rbp, rsp
  push rcx
  push r11

  ; systemv abi
  ;   rdi
  ;   rsi
  ;   rdx
  ;   rcx
  ;   r8
  ;   r9
  ;   stack (reserve)
  mov r11, rdi ; preserve arg1
  mov rdi, rax ; syscall number
  mov rax, rsi ; preserve arg2
  mov rsi, r11 ; arg1
  mov r11, rdx ; preserve arg3
  mov rdx, rax ; arg2
  mov rax, rcx ; preserve arg4
  mov rcx, r11 ; arg3
  mov r11, r8  ; preserve arg5
  mov r8,  rax ; arg4
  mov rax, r9  ; preserve arg6
  mov r9,  r11 ; arg5
  push rax     ; arg6

  xor rax, rax
  call handle_syscall

  pop r11
  pop rcx
  pop rbp

  mov KERNEL_SP, rsp
  mov rsp, USER_SP
  swapgs
  o64 sysret
