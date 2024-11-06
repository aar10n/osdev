%define DF_VECTOR     0x8
%define IDT_GATES     256
%define IDT_STUB_SIZE 48

; struct percpu offsets
%define PERCPU_INTR_LEVEL     gs:0x04
%define PERCPU_THREAD         gs:0x18
%define PERCPU_USER_SP        gs:0x40
%define PERCPU_KERNEL_SP      gs:0x48
%define PERCPU_TSS_RSP0_PTR   gs:0x50
%define PERCPU_IRQ_STACK_TOP  gs:0x58
%define PERCPU_SCRATCH_RAX    gs:0x60

; struct thread offsets
%define THREAD_FRAME(x)       [x+0x30]

; struct trapframe offsets
%define TRAPFRAME_RDI(x)      [x+0x00]
%define TRAPFRAME_RSI(x)      [x+0x08]
%define TRAPFRAME_RDX(x)      [x+0x10]
%define TRAPFRAME_RCX(x)      [x+0x18]
%define TRAPFRAME_R8(x)       [x+0x20]
%define TRAPFRAME_R9(x)       [x+0x28]
%define TRAPFRAME_RAX(x)      [x+0x30]
%define TRAPFRAME_RBX(x)      [x+0x38]
%define TRAPFRAME_RBP(x)      [x+0x40]
%define TRAPFRAME_R10(x)      [x+0x48]
%define TRAPFRAME_R11(x)      [x+0x50]
%define TRAPFRAME_R12(x)      [x+0x58]
%define TRAPFRAME_R13(x)      [x+0x60]
%define TRAPFRAME_R14(x)      [x+0x68]
%define TRAPFRAME_R15(x)      [x+0x70]
%define TRAPFRAME_FS(x)       [x+0x78]
%define TRAPFRAME_GS(x)       [x+0x7a]
%define TRAPFRAME_ES(x)       [x+0x7c]
%define TRAPFRAME_DS(x)       [x+0x7e]
%define TRAPFRAME_DATA(x)     [x+0x80]
%define TRAPFRAME_VECTOR(x)   [x+0x88]
%define TRAPFRAME_ERROR(x)    [x+0x90]
%define TRAPFRAME_RIP(x)      [x+0x98]
%define TRAPFRAME_CS(x)       [x+0xa0]
%define TRAPFRAME_RFLAGS(x)   [x+0xa8]
%define TRAPFRAME_RSP(x)      [x+0xb0]
%define TRAPFRAME_SS(x)       [x+0xb8]

%define TRAPFRAME_SIZE        0xc0


; void double_fault_handler(void)
extern double_fault_handler
; void interrupt_handler(struct trapframe *frame)
extern interrupt_handler

; ========================================
;     Interrupt Service Routine Stubs
; ========================================

; only the first 32 vectors are exceptions, and of those only some have error codes
%macro isr_stub 1
%%start:
  %if %1 == DF_VECTOR
    ; handle double fault specially because there's nothing we can do to recover and
    ; we dont want to overwrite the current threads trapframe.
    call double_fault_handler
  %else
    %if (1 << %1) & 0x27D00 == 0 ; vectors with no error code
    push qword 0  ; frame->error
    %endif

    push qword %1 ; frame->vector
    push qword 0  ; frame->data

    sub rsp, 0x80 ; bottom of trapframe
    push rax
    %if (1 << %1) & 0xb ; vectors with an ist stack
    or rax, -1 ; set ZF=0
    %else
    xor rax, rax ; set ZF=1
    %endif
    pop rax

    jmp common_interrupt_handler
  %endif

  times IDT_STUB_SIZE - ($ - %%start) db 0 ; pad to IDT_STUB_SIZE
%endmacro


global isr_stubs
isr_stubs:
  %assign i 0
%rep IDT_GATES
  isr_stub i
  %assign i i+1
%endrep


common_interrupt_handler:
  ; on interrupt the stack is pointed to the top of the trapframe of the interrupted
  ; thread. the cpu has already pushed the hardware context, so we just need to push
  ; the remaining registers. if ZF=1 the stack may need to be changed to the irq stack
  ; otherwise if ZF=0 then it uses an IST stack.
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
  mov TRAPFRAME_FS(rsp), fs
  mov TRAPFRAME_GS(rsp), gs
  mov TRAPFRAME_R15(rsp), r15
  mov TRAPFRAME_R14(rsp), r14
  mov TRAPFRAME_R13(rsp), r13
  mov TRAPFRAME_R12(rsp), r12
  mov TRAPFRAME_R11(rsp), r11
  mov TRAPFRAME_R10(rsp), r10
  mov TRAPFRAME_RBP(rsp), rbp
  mov TRAPFRAME_RBX(rsp), rbx
  mov TRAPFRAME_RAX(rsp), rax
  mov TRAPFRAME_R9(rsp), r9
  mov TRAPFRAME_R8(rsp), r8
  mov TRAPFRAME_RCX(rsp), rcx
  mov TRAPFRAME_RDX(rsp), rdx
  mov TRAPFRAME_RSI(rsp), rsi
  mov TRAPFRAME_RDI(rsp), rdi

  ; preserve the ZF flag before it is clobbered
  sete al ; al=1 (ZF=1) when the vector uses the irq stack

  ; swapgs if needed
  mov cx, ss ; SS=0 if cpl change
  test cx, cx
  jnz .0 ; dont swapgs if we are already in ring 0
  swapgs
.0:
  inc dword PERCPU_INTR_LEVEL
  cmp dword PERCPU_INTR_LEVEL, 1 ;
  sete bl ; bl=1 (ZF=1) when intr_level=1
  and al, bl ; al=1 (ZF=0) when we need to switch to the irq stack

  mov r8, PERCPU_THREAD
  mov rbp, THREAD_FRAME(r8) ; rbp=old trapframe pointer
  mov THREAD_FRAME(r8), rsp ; update thread frame pointer
  cmovnz rsp, PERCPU_IRQ_STACK_TOP ; switch to the irq stack if al=1
  push rbp ; save old trapframe pointer

  mov rdi, THREAD_FRAME(r8) ; rdi=trapframe pointer
  call interrupt_handler

  ; restore the old thread trapframe pointer
  pop rbp
  mov r8, PERCPU_THREAD
  mov rsp, THREAD_FRAME(r8)
  mov THREAD_FRAME(r8), rbp
  dec dword PERCPU_INTR_LEVEL
  ; restore the current trapframe

global trapframe_restore
trapframe_restore:
  mov ax, fs
  mov TRAPFRAME_FS(rsp), ax
  mov ax, gs
  mov TRAPFRAME_GS(rsp), ax

  mov rdi, TRAPFRAME_RDI(rsp)
  mov rsi, TRAPFRAME_RSI(rsp)
  mov rdx, TRAPFRAME_RDX(rsp)
  mov rcx, TRAPFRAME_RCX(rsp)
  mov r8, TRAPFRAME_R8(rsp)
  mov r9, TRAPFRAME_R9(rsp)
  mov rax, TRAPFRAME_RAX(rsp)
  mov rbx, TRAPFRAME_RBX(rsp)
  mov rbp, TRAPFRAME_RBP(rsp)
  mov r10, TRAPFRAME_R10(rsp)
  mov r11, TRAPFRAME_R11(rsp)
  mov r12, TRAPFRAME_R12(rsp)
  mov r13, TRAPFRAME_R13(rsp)
  mov r14, TRAPFRAME_R14(rsp)
  mov r15, TRAPFRAME_R15(rsp)

  ; swapgs if needed
  mov cx, ss ; SS=0 if cpl change
  test cx, cx
  jnz .1
  swapgs
.1:

  add rsp, 0x80
  add rsp, 24 ; skip the error, vector, and data fields
  iretq
