;
; Interrupt Handling Code
;

extern idt_handlers
extern exception_handler

%define KERNEL_OFFSET 0xFFFFFF8000000000
%define IDT_STUB_SIZE 16
%define IDT_GATES 256

%define APIC_BASE 0xFEE00000
%define APIC_REG_ID 0x20

%macro pushall 0
  ; control registers
  mov r10, cr4
  push r10
  mov r10, cr3
  push r10
  mov r10, cr2
  push r10
  mov r10, cr0
  push r10
  ; extended registers
  push r15
  push r14
  push r13
  push r12
  push r9
  push r8
  ; general registers
  push rbp
  push rsi
  push rdi
  push rdx
  push rcx
  push rbx
  push rax
%endmacro

%macro popall 0
  ; general registers
  pop rax
  pop rbx
  pop rcx
  pop rdx
  pop rdi
  pop rsi
  pop rbp
  ; extended registers
  pop r8
  pop r9
  pop r12
  pop r13
  pop r14
  pop r15
  ; control registers
  pop r10 ; cr0
  pop r10 ; cr2
  pop r10 ; cr3
  pop r10 ; cr4
%endmacro

; The follow stub is used for both exceptions
; and regular interrupts. In the case of irq's
; and exceptions which do not have an error code,
; 0 is pushed instead.
%macro interrupt_stub 1
  %%start:
  %if %1 >= 32
    push qword 0 ; empty value
  %elif (1 << %1) & 0x27D00 == 0
    push qword 0 ; dummy error code
  %endif
  push qword %1 ; vector number
  jmp common_handler
  times IDT_STUB_SIZE - ($ - %%start) db 0
%endmacro

; interrupt handler stubs that push the
; vector number onto the stack, and add
; a dummy value if no error code exists
global idt_stubs
idt_stubs:
  %assign i 0
  %rep IDT_GATES
    interrupt_stub i
  %assign i i +  1
  %endrep

; common interrupt handler which calls
; the idt handler from the idt_handlers
; array if one exists. If the interrupt
; was an exception and no handler exists,
; the default exception handler is used.
common_handler:
  pushall ; save the registers

  ; get the local apic id
;  mov rdi, APIC_BASE + APIC_REG_ID
;  mov rax, [rdi]
;  shr rax, 24
;  and rax, 0xFF
  mov rax, 0
  push rax ; apic id

  mov rdx, idt_handlers
  mov rcx, [rsp + 144] ; vector number
  mov rax, [rdx + rcx * 8] ; handler

  test rax, rax
  jz .no_handler

  mov rdi, rsp ; pass pointer to cpu_state_t
  cld          ; clear direction (required by ABI)
  call rax   ; call the handler
  jmp .interrupt_end

.no_handler:
  cmp rcx, 32
  jge .interrupt_end

  ; use the default exception handler
  mov rdi, rsp           ; pass pointer to cpu_state_t
  cld                    ; clear direction (required by ABI)
  call exception_handler ; call the handler

.interrupt_end:
  add rsp, 8  ; skip the apic id
  popall      ; restore the registers
  add rsp, 16 ; skip int_no and err_code
  iret


extern kprintf
global page_fault_handler
page_fault_handler:
  mov r15, 0xDEADBEEF
  .hang: hlt
  jmp .hang
