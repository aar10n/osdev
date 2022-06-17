; ---------------------- ;
;   Exception Handling   ;
; ---------------------- ;

%include "base.inc"
%define PAGE_FAULT 14

; ss
; rsp
; rflags
; cs
; rip
; error code
; vector

extern fault_handler
extern kprintf
extern stdio_lock
extern stdio_unlock

global page_fault_handler
page_fault_handler:
  cmp qword [rsp + 16], 0x08
  je .handle_fault
  swapgs

.handle_fault:
  mov rdi, [rsp + 8] ; faulting rip
  mov rsi, cr2       ; faulting address
  mov rdx, [rsp]     ; error code
  call fault_handler
  ; if fault_handler returns -1 it means the
  ; fault couldn't be handled in a good way
  ; so we should pass it onto the fatal
  ; exception handler below
  cmp rax, 0
  je .done
  mov rax, 0xE   ; page fault vector
  push rax
  jmp exception_handler

.done:
  add rsp, 8     ; skip the error code
  iretq

global exception_handler
exception_handler:
  sub rsp, 56
  mov [rsp + 0], rdi
  mov [rsp + 8], rsi
  mov [rsp + 16], rdx
  mov [rsp + 24], rcx
  mov [rsp + 32], r8
  mov [rsp + 40], r9
  mov [rsp + 48], rax

  ; global lock
  ;call stdio_lock

  ; get apic id
  push rbx
  mov eax, 1
  cpuid
  shr rbx, 24
  and rbx, 0xFF
  mov rax, rbx
  pop rbx

  mov rdx, exceptions
  mov rcx, qword [rsp + 56] ; vector

  mov rdi, exception_msg.1 ; format string
  mov rsi, [rdx + rcx * 8] ; exception name
  mov rdx, rax             ; apic id
  mov rcx, rcx             ; vector
  mov r8, [rsp + 64]       ; error code
  mov r9, [rsp + 72]       ; rip
  push qword [rsp + 88]    ; rflags
  call kprintf
  add rsp, 8

  mov rdi, exception_msg.2 ; format string
  mov rsi, [rsp + 48]      ; rax
  mov rdx, rbx             ; rbx
  mov rcx, [rsp + 24]      ; rcx
  mov r8, [rsp + 16]       ; rdx
  mov r9, [rsp + 0]        ; rdi
  push qword [rsp + 8]     ; rsi
  push qword [rsp + 88]    ; rsp
  push rbp                 ; rbp
  call kprintf
  add rsp, 24

  mov rdi, exception_msg.3 ; format string
  mov rsi, [rsp + 32]      ; r8
  mov rdx, [rsp + 40]      ; r9
  mov rcx, r10             ; r10
  mov r8, r11              ; r11
  mov r9, r12              ; r12
  push r13                 ; r13
  push r14                 ; r14
  push r15                 ; r15
  call kprintf
  add rsp, 24

  mov rdi, exception_msg.4 ; format string
  mov rsi, [rsp + 80]      ; cs
  mov rdx, ds              ; ds
  mov rcx, es              ; es
  mov r8, fs               ; fs
  mov r9, gs               ; gs
  push qword [rsp + 104]   ; ss
  call kprintf
  sub rsp, 8

  mov rdi, exception_msg.5 ; format string
  mov rsi, cr0             ; cr0
  mov rdx, cr2             ; cr2
  mov rcx, cr3             ; cr3
  mov r8, cr4              ; cr4
  call kprintf

  mov rdi, exception_msg.6 ; format string
  mov rsi, dr0             ; dr0
  mov rdx, dr1             ; dr1
  mov rcx, dr2             ; dr2
  mov r8, dr3              ; dr3
  mov r9, dr6              ; dr6
  mov rax, dr7             ; dr7
  push rax                 ;
  call kprintf
  add rsp, 8

  ;call stdio_unlock
.hang:
  pause
  jmp .hang


section .data

exception_msg.1: db "!!!! Exception Type - %s !!!!", 10
                 db "CPU Id: %d | Exception Code: %d | Exception Data: %#X", 10
                 db "RIP = %016llX, RFLAGS = %016llX", 10, 0
exception_msg.2: db "------------------------- GENERAL REGISTERS --------------------------", 10
                 db "RAX = %016llX, RBX = %016llX, RCX = %016llX", 10
                 db "RDX = %016llX, RDI = %016llX, RSI = %016llX", 10
                 db "RSP = %016llX, RBP = %016llX", 10, 0
exception_msg.3: db "------------------------- EXTENDED REGISTERS -------------------------", 10
                 db "R8  = %016llX, R9  = %016llX, R10 = %016llX", 10
                 db "R11 = %016llX, R12 = %016llX, R13 = %016llX", 10
                 db "R14 = %016llX, R15 = %016llX", 10, 0
exception_msg.4: db "------------------------- SEGMENT REGISTERS --------------------------", 10
                 db "CS  = %016llX, DS  = %016llX, ES  = %016llX", 10
                 db "FS  = %016llX, GS  = %016llX, SS  = %016llX", 10, 0
exception_msg.5: db "------------------------- CONTROL REGISTERS --------------------------", 10
                 db "CR0 = %016llX, CR2 = %016llX, CR3 = %016llX", 10
                 db "CR4 = %016llX", 10, 0
exception_msg.6: db "-------------------------- DEBUG REGISTERS ---------------------------", 10
                 db "DR0 = %016llX, DR1 = %016llX, DR2 = %016llX", 10
                 db "DR3 = %016llX, DR6 = %016llX, DR7 = %016llX", 10, 0

test_msg: db "Hello, world!", 10, 0

exceptions: array
  string "Division By Zero"
  string "Debug"
  string "Non Maskable Interrupt"
  string "Breakpoint"
  string "Overflow"
  string "Out of Bounds"
  string "Invalid Opcode"
  string "No Coprocessor"
  string "Double Fault"
  string "Coprocessor Segment Overrun"
  string "Bad TSS"
  string "Segment Not Present"
  string "Stack Fault"
  string "General Protection Fault"
  string "Page Fault"
  string "x87 Floating-Point Error"
  string "Alignment Check"
  string "Machine Check"
  string "SIMD Floating-Point Error"
  string "Virtualization Exception"
  string "Control Protection Exception"
endarray
