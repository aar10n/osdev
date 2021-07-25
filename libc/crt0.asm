section .text

extern main
global _start
_start:
  nop
  nop
  mov rbp, 0
  push rbp
  mov rbp, rsp

  push rdi
  push rdi

  ; initialize standard library
  ; call init_stdlib

  ; run global constructors
  ; call _init

  pop rdi
  pop rsi

  call main

  mov eax, 0 ; sys_exit
  syscall
