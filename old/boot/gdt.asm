;
; gdt.asm
;   The Global Descriptor Table
;

; ------ GDT ------
;  Null Descriptor
;  Code Descriptor
;  Data Descriptor
; -----------------

gdt_start:

; Null Descriptor
;   Structure of 8 null bytes
gdt_null:
  dd 0x0 ; 4 null bytes
  dd 0x0 ; 4 null bytes

; Code Descriptor
;   Base: 0x0
;   Limit: 0xFFFFF
;   1st Flags:
;     Present: 1
;     Privilige: 0
;       Descriptor type: 1
;   Type flags:
;     Code: 1
;     Conforming: 0
;     Readable: 1
;     Accessed: 0
;   Other flags:
;     Granularity: 1
;     32-Bit default: 1
;     64-bit code segment: 0
;     AVL: 0
gdt_code:
  ; kheap_base: 0x0
  ; limit: 0xFFFFF
  ; 1st flags: (present) 1, (privilage) 00, (descriptor type) 1 -> 1001b
  ; type flags: (code) 1, (conforming) 0, (readable) 1, (accessed) 0 -> 1010b
  ; 2nd flags: (granularity) 1, (32-bit default) 1, (64-bit code segment) 0, (AVL) 0 -> 1100b
  dw 0xFFFF ; Limit (bits 0-15)
  dw 0x00   ; Base  (bits 0-15)
  db 0x00   ; Base  (bits 16-23)
  db 0x9A   ; 1st flags, type flags
  db 0xCF   ; 2nd flags, Limit (bits 16-19)
  db 0x00   ; Base (bits 24-31)

; Data Descriptor
;   *identical to above
;   Type flags:
;     Code: 0
;     Expand down: 0
;     Writable: 1
;     Accessed: 0
gdt_data:
  ; Same as code segment except for the type flags
  ; type flags: (code) 0, (expand down) 0, (writable) 1, (accessed) 0 -> 0010b
  dw 0xFFFF ; Limit (bits 0-15)
  dw 0x00   ; Base  (bits 0-15)
  db 0x00   ; Base  (bits 16-23)
  db 0x92   ; 1st flags, type flags
  db 0xCF   ; 2nd flags, Limit (bits 16-19)
  db 0x00   ; Base (bits 24-31)

gdt_end: ; Used to calculate the size of the GDT

; GDT Descriptor
gdt_descriptor:
  dw gdt_end - gdt_start - 1 ; Size of our GDT
  dd gdt_start               ; Start address of the GDT


; GDT Segment Offset Constants
CODE_SEG equ gdt_code - gdt_start ; GDT Code Descriptor Offset
DATA_SEG equ gdt_data - gdt_start ; GDT Data Descriptor Offset
