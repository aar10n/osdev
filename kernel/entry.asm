%include "base.inc"

extern kmain
extern ap_main


%define BLUE_COLOR 0x000000FF
%define GREEN_COLOR 0x0000FF00
%define RED_COLOR 0x00FF0000

%define fb_base_offset 48
%define fb_size_offset 56

; entry happens when cpu is already in long mode with paging enabled
; it is called using sysv_abi convention
global entry
entry:
;  ;jmp $ ; hang forever
;  mov rsi, [rdi + fb_base_offset] ; get framebuffer address
;  mov rax, [rdi + fb_size_offset] ; get framebuffer size
;
;  ; divide fb_size by 4
;  xor rdx, rdx
;  mov rcx, 4
;  div rcx
;
;  ; fill screen
;  mov rcx, rax
;  xor rax, rax
;.loop:
;  mov dword [rsi + rax * 4], BLUE_COLOR
;  inc rax
;  cmp rax, rcx
;  jl .loop
;
;  jmp $ ; hang forever

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
