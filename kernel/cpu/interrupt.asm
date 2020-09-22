; defined in interrupt.c
[extern interrupt_handler]

; Common interrupt code
interrupt_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; call the C function
    call interrupt_handler

    pop ebx
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    popa
    add esp, 8
    sti
    iret

; 1 - Interrupt Request
; 2 - Vector number
%macro interrupt 2
  global isr%2
  isr%2:
    push %1
    push %2
    jmp interrupt_common
%endmacro

; Standard ISA Interrupts
interrupt  0, 32 ; Programmable Interrupt Timer Interrupt
interrupt  1, 33 ; Keybaord Interrupt
interrupt  2, 34 ; Cascade (used internally by two PICs)
interrupt  3, 35 ; COM2 (if enabled)
interrupt  4, 36 ; COM1 (if enabled)
interrupt  5, 37 ; LPT2 (if enabled)
interrupt  6, 38 ; Floppy Disk
interrupt  7, 39 ; LPT1 / Unreliable
interrupt  8, 40 ; CMOS real-time clock
interrupt  9, 41 ; Free for peripherals / legacy SCSI / NIC
interrupt 10, 42 ; Free for peripherals / SCSI / NIC
interrupt 11, 43 ; Free for peripherals / SCSI / NIC
interrupt 12, 44 ; PS2 Mouse
interrupt 13, 45 ; FPU / Coprocessor / Inter-processor
interrupt 14, 46 ; Primary ATA Hard Disk
interrupt 15, 47 ; Secondary ATA Hard Disk

%assign irq 16
%assign vec 48
%rep 208
  interrupt irq, vec ; Available
%assign irq irq + 1
%assign vec vec + 1
%endrep
