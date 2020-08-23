; Defined in isr.c
[extern isr_handler]
[extern irq_handler]

temp dd 0 ; temp variable

; Common ISR code
isr_common_stub:
  ; save control registers
  mov [temp], eax ; save eax
  mov eax, cr4    ;
  push eax        ; push cr4
  mov eax, cr3    ;
  push eax        ; push cr3
  mov eax, cr2    ;
  push eax        ; push cr2
  mov eax, cr0    ;
  push eax        ; push cr0
  mov eax, [temp] ; restore eax

  ; save general registers
  push ebp
  push esp
  push edi
  push esi
  push edx
  push ecx
  push ebx
  push eax

  ; call the C function
  call isr_handler

  ; restore general registers
  pop eax
  pop ebx
  pop ecx
  pop edx
  pop esi
  pop edi
  pop esp
  pop ebp

  add esp, 16 ; pop control registers off stack
  add esp, 8  ; pop err_code and int_no off stack
  ; return to the code that got interrupted
  iret

; Common IRQ code. Identical to ISR code except for the 'call'
; and the 'pop ebx'
irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call irq_handler ; Different than the ISR code
    pop ebx  ; Different than the ISR code
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    popa
    add esp, 8
    sti
    iret



%macro ISR 2
  global isr%2
  isr%2:
    %if %1 > -1
      push byte 0
    %endif
    push byte %2
    jmp isr_common_stub
%endmacro

%macro IRQ 2
  global irq%1
  irq%1:
    push byte %1
    push byte %2
    jmp irq_common_stub
%endmacro

; ISR's
ISR  0, 0   ; Divide Error
ISR  0, 1   ; Debug Exception
ISR  0, 2   ; NMI Interrupt
ISR  0, 3   ; Breakpoint
ISR  0, 4   ; Overflow
ISR  0, 5   ; BOUND Range Exceeded
ISR  0, 6   ; Invalid Opcode (Undefined Opcode)
ISR  0, 7   ; Device Not Available (No Math Coprocessor)
ISR -1, 8   ; Double Fault
ISR  0, 9   ; Coprocessor Segment Overrun (reserved)
ISR -1, 10  ; Invalid TSS
ISR -1, 11  ; Segment Not Present
ISR -1, 12  ; Stack-Segment Fault
ISR -1, 13  ; General Protection
ISR -1, 14  ; Page Fault
ISR  0, 15  ; Intel reserved. Do not use.
ISR  0, 16  ; x87 FPU Floating-Point Error (Math Fault)
ISR -1, 17  ; Alignment Check
ISR  0, 18  ; Machine Check
ISR  0, 19  ; SIMD Floating-Point Exception
ISR  0, 20  ; Virtualization Exception
ISR  0, 21  ; Intel reserved. Do not use.
ISR  0, 22  ; Intel reserved. Do not use.
ISR  0, 23  ; Intel reserved. Do not use.
ISR  0, 24  ; Intel reserved. Do not use.
ISR  0, 25  ; Intel reserved. Do not use.
ISR  0, 26  ; Intel reserved. Do not use.
ISR  0, 27  ; Intel reserved. Do not use.
ISR  0, 28  ; Intel reserved. Do not use.
ISR  0, 29  ; Intel reserved. Do not use.
ISR  0, 30  ; Intel reserved. Do not use.
ISR  0, 31  ; Intel reserved. Do not use.

; IRQ's
IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
