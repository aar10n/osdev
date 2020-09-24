bits 32

extern main
extern initial_esp

KERNEL_CS equ 0x08                          ; Kernel Code Segment
KERNEL_DS equ 0x10                          ; Kernel Data Segment
KERNEL_STACK_SIZE equ 0x8000                ; 32 KiB Stack Size

KERNEL_BASE equ 0xC0000000                  ; Kernel virtual location (3GB)
KERNEL_PAGE equ (KERNEL_BASE >> 22)         ; Kernel page index

; Multiboot flags
MB_ALIGN equ 0x1                            ; Align loaded modules on page boundaries
MB_INFO equ 0x2                             ; Include information about system memory
MB_VIDEO equ 0x4                            ; OS wants video mode set

MB_MAGIC equ 0x1BADB002                     ; Multiboot head magic number
MB_FLAGS equ MB_ALIGN | MB_INFO ;| MB_VIDEO ; Multiboot header flags
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)      ; Multiboot header checksum


; ------------------------------
;        Multiboot Header
; ------------------------------
section .multiboot
align 4

dd MB_MAGIC
dd MB_FLAGS
dd MB_CHECKSUM
dd 0, 0, 0, 0, 0
dd 0             ; Graphics Mode
dd 1024, 768, 32 ; Width, height, depth


; ------------------------------
;          Kernel Entry
; ------------------------------
section .text

global start
start:
  mov ecx, (initial_directory - KERNEL_BASE)
;  mov ecx, (kernel_directory - KERNEL_BASE)
  mov cr3, ecx      ; Load the page directory

  mov ecx, cr4      ;
  or ecx, (1 << 4)  ; Enable 4MB pages
  mov cr4, ecx      ;

  mov ecx, cr0      ;
  or ecx, (1 << 31) ; Enable paging
  mov cr0, ecx      ;

  ; Jump to the higher half
  lea ecx, [higher_half]
  jmp ecx

higher_half:
  mov esp, stack_top        ; Switch to the proper stack
  mov [initial_esp], esp    ; Assign the initial esp
  xor ebp, ebp              ; Ensure a null stack frame

  add ebx, KERNEL_BASE      ; Make the header virtual
  push ebx                  ; Push the header pointer

  cli                       ; Disable interrupts
  call main                 ; Start the kernel
.hang:
  hlt                       ; If the kernel exits
  jmp .hang                 ; Hang forever


; ------------------------------
;          Kernel Data
; ------------------------------
section .data
align 4096

; Identity maps nearly all of virtual memory except for
; the kernel page, which is mapped to the first 4MB of
; memory. This is only used until we configure our proper
; paging scheme, at which point we switch over to use
; the _kernel_directory
global initial_directory
initial_directory:
  %assign i 0
  %rep 1024
  %if i == 768
    dd 0b10000011
  %else
    dd (i << 22) | 0b10000011
  %endif
  %assign i i + 1
  %endrep

global kernel_directory
kernel_directory:
  %assign i 0
  %rep 1024
  %if i == 768
    dd 0b10000011
  %else
    dd (i << 22) | 0b10000011
  %endif
  %assign i i + 1
  %endrep
;  dd 0b10000011                       ; 4MB Identity Map
;  times (KERNEL_PAGE - 1) dd 0        ; Page Directory Entries
;  dd 0b10000011                       ; 4MB Kernel Area
;  times (1024 - KERNEL_PAGE - 1) dd 0 ; Page Directory Entries

global first_page_table
first_page_table:
  times 1024 dd 0

; ------------------------------
;          Kernel Stack
; ------------------------------

section .bss
align 4

global stack_bottom
stack_bottom:
resb KERNEL_STACK_SIZE
global stack_top
stack_top:
