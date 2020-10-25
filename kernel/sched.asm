;
; Context Switching
;
%include "base.inc"

; Saved registers
%macro popcallee 0
  pop rbx
  pop rbp
  pop r12
  pop r13
  pop r14
  pop r15
%endmacro

global switch_context
switch_context:
  cli
  mov rdx, current
  cmp rdx, 0
  jz .2
  cmp rdx, rdi
  jne .1
  sti
  ret

.1:
  ; save old context
  pushfq
  mov rdx, [rdx + PROC_CTX]
  mov [rdx + CTX_RBX], rbx
  mov [rdx + CTX_RBP], rbp
  mov [rdx + CTX_R12], r12
  mov [rdx + CTX_R13], r13
  mov [rdx + CTX_R14], r14
  mov [rdx + CTX_R15], r15

  pop qword [rdx + CTX_RFLAGS] ; rflags
  pop qword [rdx + CTX_RIP]    ; rip
  mov [rdx + CTX_RSP], rsp     ; rsp

.2:
  ; current = next
  mov current, rdi

  ; restore new context
  mov rsp, [rdi + PROC_CTX]
  popcallee
  sti
  iretq
