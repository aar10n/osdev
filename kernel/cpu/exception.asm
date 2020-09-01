; defined in exception.c
[extern exception_handler]

temp dd 0 ; temp variable

; Common exception code
exception_common:
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
  call exception_handler

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

; 1 - Error code
; 2 - Vector number
%macro exception 2
  global isr%2
  isr%2:
    %if %1 > -1
      push byte 0
    %endif
    push byte %2
    jmp exception_common
%endmacro

; Exceptions
exception  0, 0  ; Divide-by-zero Error (Fault)
exception  0, 1  ; Debug (Fault/Trap)
exception  0, 2  ; Non-maskable Interrupt (Interrupt)
exception  0, 3  ; Breakpoint (Trap)
exception  0, 4  ; Overflow (Trap)
exception  0, 5  ; Bound Range Exceeded (Fault)
exception  0, 6  ; Invalid Opcode (Fault)
exception  0, 7  ; Device Not Available (Fault)
exception -1, 8  ; Double Fault (Abort)
exception  0, 9  ; Intel Reserved
exception -1, 10 ; Invalid TSS (Fault)
exception -1, 11 ; Segment Not Present (Fault)
exception -1, 12 ; Stack-Segment Fault (Fault)
exception -1, 13 ; General Protection (Fault)
exception -1, 14 ; Page Fault (Fault)
exception  0, 15 ; Intel Reserved
exception  0, 16 ; x87 FPU Floating-Point Exception (Fault)
exception -1, 17 ; Alignment Check (Fault)
exception  0, 18 ; Machine Check (Abort)
exception  0, 19 ; SIMD Floating-Point Exception (Fault)
exception  0, 20 ; Virtualization Exception (Fault)
exception  0, 21 ; Intel Reserved
exception  0, 22 ; Intel Reserved
exception  0, 23 ; Intel Reserved
exception  0, 24 ; Intel Reserved
exception  0, 25 ; Intel Reserved
exception  0, 26 ; Intel Reserved
exception  0, 27 ; Intel Reserved
exception  0, 28 ; Intel Reserved
exception  0, 29 ; Intel Reserved
exception  0, 30 ; Security Exception
exception  0, 31 ; Intel Reserved

