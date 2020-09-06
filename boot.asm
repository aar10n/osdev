bits 32

; Kernel main
extern main


;
; Constants
;

; Kernel
KERNEL_CS equ 0x08                     ; Kernel Code Segment
KERNEL_DS equ 0x10                     ; Kernel Data Segment
STACK_SIZE equ 0x8000                  ; 16KB Stack Size

; Multiboot Header
_ALIGN equ 0x1                         ; Align loaded modules on page boundaries
_INFO equ 0x2                          ; Include information about system memory
_VIDINFO equ 0x4                       ; OS wants video mode set

MB_MAGIC equ 0x1BADB002                ; Multiboot head magic number
MB_FLAGS equ _ALIGN | _INFO;| _VIDINFO ; Multiboot header flags
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS) ; Multiboot header checksum

KERNEL_BASE equ 0xC0000000             ; Kernel virtual location (3GB)
KERNEL_PAGE equ (KERNEL_BASE >> 22)    ; Kernel page index


;
; Data
;
section .data
align 0x1000

; Setup the page directory
global _page_directory
_page_directory:
  dd 0b10000011                       ; 4MB Identity Map
  times (KERNEL_PAGE - 1) dd 0        ; Page Directory Entries
  dd 0b10000011                       ; 4MB Kernel Area
  times (1024 - KERNEL_PAGE - 1) dd 0 ; Page Directory Entries

;global _kernel_page_table
;_kernel_page_table:
;  times 1024 dd 0

;
; Text
;
section .text
align 4

; Create the multiboot header
multiboot:
  dd MB_MAGIC
  dd MB_FLAGS
  dd MB_CHECKSUM
  dd 0, 0, 0, 0, 0
  dd 0 ; Mode
  dd 1024, 768, 32

; Entry point
global _start
_start:
  mov ecx, (_page_directory - KERNEL_BASE)
  mov cr3, ecx      ; Load the page directory

  mov ecx, cr4
  or ecx, (1 << 4)  ; Enable 4MB pages
  mov cr4, ecx

  mov ecx, cr0
  or ecx, (1 << 0)  ; Enable Protected Mode
  or ecx, (1 << 31) ; Enable Paging
  mov cr0, ecx

  ; Absolute jump to higher half
  lea ecx, [_higher_half]
  jmp ecx

_higher_half:
  ; Unmap the first 4MB of memory
  mov dword [_page_directory], 0
  invlpg [0]

  mov eax, cr3 ; invlpg
  mov cr3, eax ;

  mov esp, kernel_stack_top ; Setup the stack pointer
  push eax                  ; Push the multiboot header

  add ebx, KERNEL_BASE      ; Make the header virtual
  push ebx                  ; Push the header pointer

  cli                       ; Clear interrupts
  call main                 ; Enter the kernel
.hang:
  hlt                       ; Halt if the kernel exits
  jmp .hang                 ; Hang forever


;
; Bss
;
section .bss

global kernel_stack_bottom
global kernel_stack_top
kernel_stack_bottom:
    resb STACK_SIZE ; Reserve 16KB for kernel stack
kernel_stack_top:
