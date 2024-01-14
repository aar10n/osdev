;
; Interrupt Handling
;

extern double_fault_handler ; void(void)
extern interrupt_handler ; void(struct trapframe *frame)

%define KERNEL_CS     0x8

%define DF_VECTOR     0x8
%define IDT_GATES     256
%define IDT_STUB_SIZE 32

%define PUSHALL_COUNT 15
%define STACK_OFFSET(x) ((x) * 8)


; this macro calls swapgs only if the protection level has changed from user
%macro swapgs_if_needed 1
  cmp word [rsp + %1], KERNEL_CS
  je %%skip
  swapgs
  %%skip:
%endmacro

; only the first 32 vectors are exceptions, and of those only some have error codes.
; push a dummy value if no error code exists so we can use a common isr handler.
%macro isr_stub 1
%%start:

  %if %1 == DF_VECTOR
    ; handle double fault specially because there's nothing we can do to recover and
    ; we dont want to overwrite the current threads trapframe.
    call double_fault_handler
  %else
    %if (1 << %1) & 0x27D00 == 0
    push qword 0  ; frame->error
    %endif
    push qword %1 ; frame->vector
    push qword 0  ; frame->data
    jmp __isr_handle
  %endif

  times IDT_STUB_SIZE - ($ - %%start) db 0 ; pad to IDT_STUB_SIZE
%endmacro


global __isr_stubs
__isr_stubs:
  %assign i 0
%rep IDT_GATES
  isr_stub i
  %assign i i+1
%endrep


__isr_handle:
  ; the interrupt should have caused our stack to switch to the top of the trapframe
  ; of the interrupted thread. the cpu will have already pushed the values into the
  ; frame, so we just need to push the remaining register values.
  ;
  ; ---- top of trapframe ----
  ;    +56  ss
  ;    +48  rsp
  ;    +40  rflags
  ;    +32  cs
  ;    +24  rip
  ;    +16  error
  ;     +8  vector
  ; rsp ->  data
  swapgs_if_needed STACK_OFFSET(4)
  push r15
  push r14
  push r13
  push r12
  push r11
  push r10
  push rbp
  push rbx
  push rax
  push r9
  push r8
  push rcx
  push rdx
  push rsi
  push rdi

  mov rdi, rsp
  call interrupt_handler
  ; restore

global __restore_trapframe
__restore_trapframe:
  pop rdi
  pop rsi
  pop rdx
  pop rcx
  pop r8
  pop r9
  pop rax
  pop rbx
  pop rbp
  pop r10
  pop r11
  pop r12
  pop r13
  pop r14
  pop r15
  swapgs_if_needed STACK_OFFSET(4)

  add rsp, STACK_OFFSET(3) ; skip vector+error+data
  iretq
