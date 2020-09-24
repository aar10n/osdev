extern ap_main
;extern initial_directory
extern kernel_directory

KERNEL_CS equ 0x08
KERNEL_BASE equ 0xC0000000
KERNEL_PAGE equ (KERNEL_BASE >> 22)

SMPBOOT_START equ 0x7000

%define label(lbl) (SMPBOOT_START + (lbl - ap_start))

section .text

bits 16
global ap_start
ap_start:
  ; 16-bit real mode
  cli

  mov word [label(ap_end) + 4], 0xFACE
  mov byte [label(test_val)], 0x02

  lgdt [label(gdt)]

  mov eax, cr0     ;
  or ecx, (1 << 0) ; Enable protected mode
  mov cr0, ecx     ;

  ; Point to our gdt data segment
  mov ax, 0x10
  mov ds, ax
  mov ss, eax

  ; Long jump into protected mode
  jmp KERNEL_CS:label(ap_start32)


bits 32
ap_start32:
  ; 32-bit protected mode
  mov dword [label(ap_end) + 8], 0xDEADBEEF

;  mov ecx, (initial_directory - KERNEL_BASE)
  mov ecx, (kernel_directory - KERNEL_BASE)
  mov cr3, ecx      ; Load the page directory

  mov ecx, cr4      ;
  or ecx, (1 << 4)  ; Enable 4MB pages
  mov cr4, ecx      ;

  mov ecx, cr0      ;
  or ecx, (1 << 31) ; Enable paging
  mov cr0, ecx      ;

  mov byte [label(ap_end) + 12], 'P'
  mov byte [label(ap_end) + 13], 'A'
  mov byte [label(ap_end) + 14], 'G'
  mov byte [label(ap_end) + 15], 'E'

  ; Jump to the higher half
  lea ecx, [ap_higher_half]
  jmp ecx

ap_higher_half:
  mov dword [label(ap_end) + 16], 0xDEADBEEF

  mov esp, ap_stack_top  ; Switch to the proper stack
  xor ebp, ebp           ; Ensure a null stack frame

  call ap_main
.hang:
  hlt
  jmp [label(.hang)]


; Data

align 4

gdt:
  dw gdt_end - gdt - 1 ; limit
  dd label(.gdt_null)   ; base

.gdt_null:
  dq 0x0000

.gdt_code:
  dw 0xFFFF ; limit first 0-15 bits
  dw 0      ; base first 0-15 bits
  db 0      ; base 16-23 bits
  db 0x9A   ; access byte
  db 0xCF   ; flags
  db 0      ; base 24-31 bits

.gdt_data:
  dw 0xFFFF ; limit first 0-15 bits
  dw 0      ; base first 0-15 bits
  db 0      ; base 16-23 bits
  db 0x92   ; access byte
  db 0xCF   ; flags
  db 0      ; base 24-31 bits
gdt_end:

global test_val
test_val: db 5

global ap_end
ap_end:


section .bss
align 4

global ap_stack_bottom
ap_stack_bottom:
resb 0x4000
global ap_stack_top
ap_stack_top:
