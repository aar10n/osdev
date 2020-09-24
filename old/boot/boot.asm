;
; main.asm
;   A simple boot sector for a
;   barebones operating system
;

%include "util.asm"

; Boot Sector Start
[org 0x7c00]
  KERNEL_OFFSET equ 0x1000
  
  mov [BOOT_DRIVE], dl

  mov bp, 0x8000 ; Set the stack kheap_base pointer leaving enough
  mov sp, bp     ; room as to not overwrite the boot sector

  println MESSAGE_MODE_REAL

  call load_kernel
  println_hex [KERNEL_OFFSET]
  call load_protected_mode

  jmp $

; load_kernel - Load the kernel code
load_kernel:
  println MESSAGE_LOAD_KERN

  mov bx, KERNEL_OFFSET ; Into the address `KERNEL_OFFSET`
  mov cl, 10            ; Start at sector 10
  mov dh, 20            ; Load 10 sectors
  mov dl, [BOOT_DRIVE]  ; From the boot drive
  call disk_load        ;
  ret


; Include utilities
%include "print.asm"
%include "disk.asm"

; Include the GDT and IDT
%include "gdt.asm"
; %include "idt.asm"

; Include protected mode
%include "pm.asm"

[bits 32]
; init_kernel - Use 32-bit mode
init_kernel:
  call KERNEL_OFFSET ; Initialize the kernel
  jmp $


; Global Variables
BOOT_DRIVE: db 0
MESSAGE_MODE_REAL: db 'Started in real mode.', 0
MESSAGE_LOAD_KERN: db 'Loading kernel...', 0

; Boot Sector End
times 510-($-$$) db 0 ; Fill with empty 0's
dw 0xAA55 ; Boot sector byte mark
