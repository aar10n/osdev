%include "base.inc"

%macro memset_slow 2
  mov rcx, rdi
  test rdx, rdx
  jz .end
.loop:
  mov [rdi], %1
  add rdi, %2
  sub rdx, 1
  jnz .loop
.end:
%endmacro

%macro memset_fast_top 1
  push rbp                     ;
  mov rbp, rsp                 ;
  mov qword [rbp - 8], rdi     ; save dest
  mov qword [rbp - 16], rsi    ; save val
  mov qword [rbp - 24], rdx    ; save len
  mov rax, %1
  xor rdx, rdx
  imul qword [rbp - 24]
  mov qword [rbp - 24], rax
; check destination alignment
  xor rdx, rdx                 ;
  mov rax, rdi                 ; rax <- dest
  mov rcx, 128                 ;
  idiv qword rcx               ; dest % 128 (rdx <- remainder)
  xor rdx, rdx                 ; check if rdx is 0
  jz .skip_0                   ; use unaligned if dest is not 16-byte aligned
  mov qword r8, __memset_fast_unaligned
.skip_0:                       ;
  mov qword r8, __memset_fast_aligned
; determine how many 16-byte chunks there are
  mov qword rax, [rbp - 24]    ; rax <- len
  mov rcx, 16                  ;
  idiv rcx                     ; len / %1 (rax <- quotient, rdx <- remainder)
  and rax, rax                 ; check if rax is 0
  jz .memset_slow              ; use slow memset if len < 16
  mov r9, rax                  ; save quotient
  mov r10, rdx                 ; save remainder
%endmacro

%macro memset_fast_bottom 2
; use slow memset on any remaining bytes
  mov qword [rbp - 24], r10    ; len <- remainder
.memset_slow:
  mov qword rsi, [rbp - 16]    ; rsi <- val
  mov qword rdx, [rbp - 24]    ; rdx <- len
  mov rcx, rdi
  test rdx, rdx
  jz .end
.loop:
  mov [rdi], %1
  add rdi, %2
  sub rdx, 1
  jnz .loop
.end:
  mov rax, rcx                 ;
  pop rbp                      ;
%endmacro


; __memset_fast_aligned - memset into 16-byte aligned memory
; dest = rdi, len = r9, val = xmm0
__memset_fast_aligned:
.loop:
  movntdq [rdi], xmm0
  add rdi, 16
  sub r9, 1
  jnz .loop
  ret

; __memset_fast_unaligned - memset into unaligned memory
; dest = rdi, len = r9, val = xmm0
__memset_fast_unaligned:
.loop:
  movdqu [rdi], xmm0
  add rdi, 16
  sub r9, 1
  jnz .loop
  ret

;
;
;

; void *__memset_slow(void *dest, int val, size_t len)
global __memset_slow
__memset_slow:
  mov rcx, rdi
  test rdx, rdx
  jz .end
.loop:
  mov [rdi], sil
  add rdi, 1
  sub rdx, 1
  jnz .loop
.end:
  mov rax, rcx
  ret

; void *__memset8(void *dest, uint8_t val, size_t len)
global memset
memset:
global __memset8
__memset8:
  memset_fast_top 1
; ------------------
  mov qword rax, 0x0101010101010101
  imul rax, rsi                ; repeat byte 8 times
  movq xmm0, rax               ; load bytes into xmm0
  movddup xmm0, xmm0           ; repeat sequence again (16 repeated bytes)
  call r8                      ; call memset loop
; ------------------
  memset_fast_bottom sil, 1
  ret

; void *__memset32(void *dest, uint32_t val, size_t len)
global __memset32
__memset32:
  memset_fast_top 4
; ------------------
  mov rax, rsi
  shl rax, 32
  or rax, rsi
  movq xmm0, rax               ; load dwords into xmm0
  movddup xmm0, xmm0           ; repeat sequence again (4 repeated dwords)
  call r8                      ; call memset loop
; ------------------
  memset_fast_bottom eax, 4
  ret

; void *__memset64(void *dest, uint64_t val, size_t len)
global __memset64
__memset64:
  memset_fast_top 8
; ------------------
  mov rax, rsi
  movq xmm0, rax               ; load qword into xmm0
  movddup xmm0, xmm0           ; repeat qword again (2 repeated qwords)
  call r8                      ; call memset loop
; ------------------
  memset_fast_bottom rax, 8
  ret
