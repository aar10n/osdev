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

  push rbp
  mov rbp, rsp
  pushcaller

  ; registers on entry
  ;   rax   syscall number
  ;   rcx   return address
  ;   r11   saved rflags
  ;   rdi   arg1
  ;   rsi   arg2
  ;   rdx   arg3
  ;   r10   arg4
  ;   r8    arg5
  ;   r9    arg6
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
  ; systemv abi
  ;   rdi (syscall number)
  ;   rsi (arg1)
  ;   rdx (arg2)
  ;   rcx (arg3)
  ;   r8  (arg4)
  ;   r9  (arg5)
  ;   stack (arg6)

  call handle_syscall

  pop r11 ; arg6
  popcaller
  pop rbp

  mov KERNEL_SP, rsp
  mov rsp, USER_SP
  swapgs
  o64 sysret
