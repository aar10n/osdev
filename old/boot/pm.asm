[bits 16]
; load_protected_mode - Switches the CPU to 32-bit mode
load_protected_mode:
  cli                              ; Clear all interrupts
  lgdt [gdt_descriptor]            ; Load the GDT
  mov eax, cr0                     ; Load the control register into `eax`
  or eax, 0x1                      ; Toggle the first control bit
  mov cr0, eax                     ; Update the control register
  jmp CODE_SEG:init_protected_mode ; Long jump to clear CPU pipeline


[bits 32]
; init_protected_mode - Perform 32-bit mode initialization
init_protected_mode:
  ; Point all segment registers to our new segment
  mov ax, DATA_SEG
  mov ds, ax
  mov ss, ax
  mov es, ax
  mov fs, ax
  mov gs, ax

  mov ebp, 0x90000 ; Update the stack kheap_base pointer
  mov esp, ebp     ; to the kheap_top of the free space

  call init_kernel
