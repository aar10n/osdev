%define DF_VECTOR     0x8
%define IDT_GATES     256
%define IDT_STUB_SIZE 48

; struct percpu offsets
%define PERCPU_INTR_LEVEL     gs:0x04
%define PERCPU_PREEMPTED      gs:0x06
%define PERCPU_THREAD         gs:0x18
%define PERCPU_USER_SP        gs:0x40
%define PERCPU_KERNEL_SP      gs:0x48
%define PERCPU_TSS_RSP0_PTR   gs:0x50
%define PERCPU_IRQ_STACK_TOP  gs:0x58
%define PERCPU_SCRATCH_RAX    gs:0x60

; struct thread offsets
%define THREAD_TCB(x)         [x+0x28]
%define THREAD_FRAME(x)       [x+0x30]

; struct tcb offsets
%define TCB_RIP(x)            [x+0x00]
%define TCB_RSP(x)            [x+0x08]
%define TCB_RBP(x)            [x+0x10]
%define TCB_RBX(x)            [x+0x18]
%define TCB_R12(x)            [x+0x20]
%define TCB_R13(x)            [x+0x28]
%define TCB_R14(x)            [x+0x30]
%define TCB_R15(x)            [x+0x38]
%define TCB_RFLAGS(x)         [x+0x40]
%define TCB_FSBASE(x)         [x+0x48]
%define TCB_GSBASE(x)         [x+0x50]
%define TCB_DR0(x)            [x+0x58]
%define TCB_DR1(x)            [x+0x60]
%define TCB_DR2(x)            [x+0x68]
%define TCB_DR3(x)            [x+0x70]
%define TCB_DR6(x)            [x+0x78]
%define TCB_DR7(x)            [x+0x80]
%define TCB_FPUSTATE(x)       [x+0x88]
%define TCB_FLAGS(x)          [x+0x90]

%define TCB_SIZE              0xa0 ; 0x98 aligned to 16 bytes

; struct trapframe offsets
%define TRAPFRAME_PARENT(x)   [x+0x00]
%define TRAPFRAME_FLAGS(x)    [x+0x08]
%define TRAPFRAME_RDI(x)      [x+0x10]
%define TRAPFRAME_RSI(x)      [x+0x18]
%define TRAPFRAME_RDX(x)      [x+0x20]
%define TRAPFRAME_RCX(x)      [x+0x28]
%define TRAPFRAME_R8(x)       [x+0x30]
%define TRAPFRAME_R9(x)       [x+0x38]
%define TRAPFRAME_RAX(x)      [x+0x40]
%define TRAPFRAME_RBX(x)      [x+0x48]
%define TRAPFRAME_RBP(x)      [x+0x50]
%define TRAPFRAME_R10(x)      [x+0x58]
%define TRAPFRAME_R11(x)      [x+0x60]
%define TRAPFRAME_R12(x)      [x+0x68]
%define TRAPFRAME_R13(x)      [x+0x70]
%define TRAPFRAME_R14(x)      [x+0x78]
%define TRAPFRAME_R15(x)      [x+0x80]
%define TRAPFRAME_FS(x)       [x+0x88]
%define TRAPFRAME_GS(x)       [x+0x8a]
%define TRAPFRAME_ES(x)       [x+0x8c]
%define TRAPFRAME_DS(x)       [x+0x8e]
%define TRAPFRAME_DATA(x)     [x+0x90]
%define TRAPFRAME_VECTOR(x)   [x+0x98]
%define TRAPFRAME_ERROR(x)    [x+0xa0]
%define TRAPFRAME_RIP(x)      [x+0xa8]
%define TRAPFRAME_CS(x)       [x+0xb0]
%define TRAPFRAME_RFLAGS(x)   [x+0xb8]
%define TRAPFRAME_RSP(x)      [x+0xc0]
%define TRAPFRAME_SS(x)       [x+0xc8]

%define TRAPFRAME_SIZE        0xd0
; amount to subtract from interrupt stack pointer to get to the bottom of the trapframe
%define TRAPFRAME_FIX_OFF     0x90

; trapframe flag bits
%define TF_SYSRET   0 ; needs sysret

%define SCHED_PREEMPTED       0


; void double_fault_handler()
extern double_fault_handler
; void infinite_loop_handler(void *gs_base, void *kernel_gs_base)
extern infinite_loop_handler
; void interrupt_handler(struct trapframe *frame)
extern interrupt_handler
; void sched_again(sched_reason_t reason)
extern sched_again

; DEBUGGING
section .data
interrupt_nest_count dq 0 ; used to track interrupt nesting level

section .text

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

    ; DB, NMI and DF have IST stacks
    %if (1 << %1) & 0x106
    push qword 1 ; has_ist=1
    %else
    push qword 0 ; has_ist=0
    %endif

    push rax ; save rax on stack until we can move it to PERCPU_SCRATCH_RAX
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
  ; on interrupt the stack we are operating is one of the following:
  ;   - the thread kernel stack (interupt from kernel mode, not changed)
  ;   - the thread trapframe (interupt from user mode, loaded from tss->rsp0)
  ;   - an IST stack (interrupt from DBG, NMI or DF exception)
  ;   - the irq stack (nested interrupt)
  ; the irq stack needs to be switched to if we are not already on it and the
  ; interrupt does not use an ist stack.
  ;
  ; it's important the original thread that was interrupted points at the correct
  ; trapframe so it can be restored if preempted. if an interrupt occurs while in
  ; kernel mode, the stack pointer is not changed to the thread trapframe and so
  ; it creates a new trapframe on the kernel stack. then we store the original
  ; thread trapframe in this new frames parent field so it can be restored.
  ;
  ; ---- top of cpu frame ----
  ;    +72  ss
  ;    +64  rsp
  ;    +56  rflags
  ;    +48  cs
  ;    +40  rip
  ;    +32  error
  ;    +24  vector
  ;    +16  data  <- start of the "real" trapframe
  ;     +8  saved has_ist
  ; rsp ->  saved rax

  ; INTERRUPT LOOP DEBUGGING
  inc qword [rel interrupt_nest_count] ; increment the nested interrupt count
  cmp qword [rel interrupt_nest_count], 3 ; check if we are in a nested interrupt
  jl .skip_loop_debug

  ; load gs_base into rdi
  mov ecx, 0xC0000101    ; IA32_GS_BASE MSR
  rdmsr                  ; Read MSR (result in EDX:EAX)
  shl rdx, 32            ; Shift high 32 bits
  or rax, rdx            ; Combine into 64-bit value
  mov rdi, rax           ; Store gs.base in rdi

  ; load kernel_gs_base into rsi
  mov ecx, 0xC0000102    ; IA32_KERNEL_GS_BASE MSR
  rdmsr                  ; Read MSR (result in EDX:EAX)
  shl rdx, 32            ; Shift high 32 bits
  or rax, rdx            ; Combine into 64-bit value
  mov rsi, rax           ; Store kernel_gs_base in rsi

  ; we are in a nested interrupt loop
  ; align rsp to 16 bytes
  and rsp, -16
  call infinite_loop_handler
.skip_loop_debug:

  ; swapgs if we came from user mode
  mov word ax, [rsp+48] ; ax = pushed CS
  and ax, 3 ; mask CPL bits (CPL=0 -> ZF=1, CPL=3 -> ZF=0)
  jz .noswapgs  ; we were in kernel mode (ZF=1)
  swapgs        ; we were in user mode (ZF=0)
.noswapgs:
  pop rax ; pop saved rax
  mov qword PERCPU_SCRATCH_RAX, rax ; move to percpu scratch
  pop rax ; pop saved has_ist
  ; now rsp points to the pushed frame

  ; if we came from user mode, we are already using the thread trapframe
  ; loaded from the tss rsp0. ZF is still valid from the previous test
  jnz .done_stack_change ; we were in user mode (ZF=0)

  ; if we came from kernel mode, we only should switch stacks if
  ; we are not already on an IST stack or in a nested interrupt.

  ; check if we are on an ist stack
  test rax, rax ; has_ist=0 -> ZF=1, has_ist=1 -> ZF=0
  jnz .done_stack_change ; we are on an ist stack (ZF=0)

  ; check if we are in a nested interrupt
  mov word ax, PERCPU_INTR_LEVEL ; ax = percpu intr_level
  test ax, ax ; intr_level=0 -> ZF=1, intr_level>0 -> ZF=0
  jnz .done_stack_change ; we are in a nested interrupt (ZF=0)

  ; we need to switch to the irq stack
  mov rax, rsp ; rax = current rsp
  mov rsp, PERCPU_IRQ_STACK_TOP ; switch to the irq stack

  ; move the existing pushed state into the new trapframe on the irq stack
  ; ---- top of old trapframe ----
  ;    +56  ss
  ;    +48  rsp
  ;    +40  rflags
  ;    +32  cs
  ;    +24  rip
  ;    +16  error
  ;     +8  vector
  ; rax ->  data
  ;
  ; ---- top of new trapframe ----
  ; rsp ->  ss
  ;         rsp
  ;         rflags
  ;         cs
  ;         rip
  ;         error
  ;         vector
  ;         data
  push qword [rax+56] ; push ss
  push qword [rax+48] ; push rsp
  push qword [rax+40] ; push rflags
  push qword [rax+32] ; push cs
  push qword [rax+24] ; push rip
  push qword [rax+16] ; push error
  push qword [rax+8]  ; push vector
  push qword [rax]    ; push data
  ; the stack at rsp is now equivalent to the original frame

.done_stack_change:
  ; at this point rsp is on the correct stack
  ; ---- top of old trapframe ----
  ;    +56  ss
  ;    +48  rsp
  ;    +40  rflags
  ;    +32  cs
  ;    +24  rip
  ;    +16  error
  ;     +8  vector
  ; rsp ->  data
  inc word PERCPU_INTR_LEVEL ; increment the interrupt level
  sub rsp, TRAPFRAME_FIX_OFF ; point rsp at the bottom of the trapframe

  push r14 ; preserve r14
  push r15 ; preserve r15

  ; point the thread at current trapframe and save pointer to old one
  mov r14, PERCPU_THREAD     ; r14 = current thread
  mov r15, THREAD_FRAME(r14) ; r15 = old trapframe
  mov rax, rsp ; rax = rsp
  add rax, 16 ; fix rsp for current trapframe (above pushed r14 and r15)
  mov THREAD_FRAME(r14), rax ; thread->frame = current trapframe

  ; only set the parent pointer if it is different from the current trapframe
  cmp rax, r15 ; compare current trapframe with the old one
  mov rax, 0   ; rax = NULL (default)
  cmovne rax, r15 ; rax = parent trapframe if rax != r15

  pop r15 ; restore r15
  pop r14 ; restore r14

  mov TRAPFRAME_PARENT(rsp), rax ; frame->parent = rax
  mov dword TRAPFRAME_FLAGS(rsp), 0 ; clear trapframe flags

  ; save the rest of the thread state
  mov TRAPFRAME_FS(rsp),  fs
  mov TRAPFRAME_GS(rsp),  gs
  mov TRAPFRAME_R15(rsp), r15
  mov TRAPFRAME_R14(rsp), r14
  mov TRAPFRAME_R13(rsp), r13
  mov TRAPFRAME_R12(rsp), r12
  mov TRAPFRAME_R11(rsp), r11
  mov TRAPFRAME_R10(rsp), r10
  mov TRAPFRAME_RBP(rsp), rbp
  mov TRAPFRAME_RBX(rsp), rbx
  mov rbx, PERCPU_SCRATCH_RAX ; restore saved rax
  mov TRAPFRAME_RAX(rsp), rbx
  mov TRAPFRAME_R9(rsp),  r9
  mov TRAPFRAME_R8(rsp),  r8
  mov TRAPFRAME_RCX(rsp), rcx
  mov TRAPFRAME_RDX(rsp), rdx
  mov TRAPFRAME_RSI(rsp), rsi
  mov TRAPFRAME_RDI(rsp), rdi

  mov r15, rsp ; r15 = trapframe (and unaligned rsp)
  mov rdi, r15 ; rdi = trapframe (arg 1 for interrupt_handler)
  and rsp, -16 ; align rsp to 16 bytes
  call interrupt_handler

  ; INTERRUPT LOOP DEBUGGING
  dec qword [rel interrupt_nest_count] ; decrement the nested interrupt count

  ; check if we are exiting the last interrupt
  sub word PERCPU_INTR_LEVEL, 1 ; intr_level=0 -> ZF=1, intr_level>0 -> ZF=0
  setz bl ; bl = 1 if we are exiting the last interrupt
  and byte bl, PERCPU_PREEMPTED ; bl = 1 if we are also preempted
  jz .do_trapframe_restore ; restore if not preempted

  ; ============ preemption ============
  mov byte PERCPU_PREEMPTED, 0 ; clear the percput preempted flag
  ; with the preempted thread state saved in the trapframe we can reschedule to another
  ; thread and restore from the trapframe when this thread is scheduled next
  mov rdi, SCHED_PREEMPTED
  call sched_again
  ; this may return but only if there are no non-idle threads to run

.do_trapframe_restore:
  mov rsp, r15 ; rsp = restored trapframe pointer
  mov r8, PERCPU_THREAD ; r8 = thread pointer

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; trapframe_restore:
;   rsp - trapframe pointer
;   r8  - thread pointer
;
global trapframe_restore
trapframe_restore:
  ; restore the parent trapframe pointer
  mov rax, TRAPFRAME_PARENT(rsp) ; rax = parent trapframe
  mov rbx, THREAD_FRAME(r8) ; rbx = current trapframe
  test rax, rax ; check if parent is NULL
  cmovnz rbx, rax ; rbx = parent trapframe if rax != NULL
  mov THREAD_FRAME(r8), rbx

;  mov ax, TRAPFRAME_FS(rsp)
;  mov fs, ax
;  mov ax, TRAPFRAME_GS(rsp)
;  mov gs, ax

  ; restore from current trapframe
  mov rdi, TRAPFRAME_RDI(rsp)
  mov rsi, TRAPFRAME_RSI(rsp)
  mov rdx, TRAPFRAME_RDX(rsp)
  mov rcx, TRAPFRAME_RCX(rsp)
  mov r8,  TRAPFRAME_R8(rsp)
  mov r9,  TRAPFRAME_R9(rsp)
  mov rax, TRAPFRAME_RAX(rsp)
  mov rbx, TRAPFRAME_RBX(rsp)
  mov rbp, TRAPFRAME_RBP(rsp)
  mov r10, TRAPFRAME_R10(rsp)
  mov r11, TRAPFRAME_R11(rsp)
  mov r12, TRAPFRAME_R12(rsp)
  mov r13, TRAPFRAME_R13(rsp)
  mov r14, TRAPFRAME_R14(rsp)
  mov r15, TRAPFRAME_R15(rsp)

  ; check if we need to return with sysret
  bt dword TRAPFRAME_FLAGS(rsp), TF_SYSRET
  jc .restore_sysret

  ; swapgs if needed
  cmp word TRAPFRAME_CS(rsp), 0x8 ; check if we are in kernel mode
  jz .restore_iretq ; we are in kernel mode (CS=0x8)
  swapgs ; we are going back to user mode
.restore_iretq:
  add rsp, TRAPFRAME_FIX_OFF ; adjust back to pushed frame
  add rsp, 24 ; skip the error, vector, and data fields
  iretq

.restore_sysret:
    ; clear the sysret flag
    and dword TRAPFRAME_FLAGS(rsp), ~(1 << TF_SYSRET)

    mov rcx, TRAPFRAME_RIP(rsp)     ; loads rip from rcx
    mov r11, TRAPFRAME_RFLAGS(rsp)  ; loads rflags from r11

    ; restore user stack
    mov rsp, TRAPFRAME_RSP(rsp)
    swapgs
    o64 sysret
