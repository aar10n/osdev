; https://github.com/freebsd/freebsd-src/blob/main/sys/amd64/amd64/cpu_switch.S

; struct percpu offsets
%define PERCPU_SPACE          gs:0x10
%define PERCPU_THREAD         gs:0x18
%define PERCPU_PROCESS        gs:0x20
%define PERCPU_USER_SP        gs:0x40
%define PERCPU_KERNEL_SP      gs:0x48
%define PERCPU_TSS_RSP0_PTR   gs:0x50
%define PERCPU_IRQ_STACK_TOP  gs:0x58
%define PERCPU_RFLAGS         gs:0x68

; struct proc offsets
%define PROCESS_SPACE(x)      [x+0x08]

; struct address space offsets
%define ADDR_SPACE_PGTABLE(x) [x+0x48]

; struct thread offsets
%define THREAD_TID(x)         [x+0x00]
%define THREAD_FLAGS(x)       [x+0x04]
%define THREAD_PROC(x)        [x+0x20]
%define THREAD_TCB(x)         [x+0x28]
%define THREAD_FRAME(x)       [x+0x30]
%define THREAD_KSTACK_BASE(x) [x+0x38]
%define THREAD_KSTACK_SIZE(x) [x+0x40]
%define THREAD_FLAGS2(x)      [x+0x90]
%define THREAD_KSTACK_PTR(x)  [x+0x98]
%define THREAD_USTACK_PTR(x)  [x+0xA0]

; thread flags2 bits
%define TDF2_SIGPEND          3
%define TDF2_TRAPFRAME        4

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

; TCB flag bits
%define TCB_KERNEL  0 ; kernel context
%define TCB_FPU     1 ; save fpu registers
%define TCB_DEBUG   2 ; save debug registers
%define TCB_SYSRET  3 ; needs sysret

%define TCB_SIZE        0xa0 ; 0x98 aligned to 16 bytes
%define TRAPFRAME_SIZE  0xd0

%define FSBASE_MSR  0xC0000100
%define GSBASE_MSR  0xC0000101
%define KGSBASE_MSR 0xC0000102

; top of the thread stack sits below the trapframe and tcb
%define STACK_TOP_OFF   (TCB_SIZE + TRAPFRAME_SIZE)

; void signal_dispatch()
;   defined in signal.c
extern signal_dispatch

; void switch_address_space(address_space_t *new_space)
;   defined in vmalloc.c
extern switch_address_space

; void _thread_unlock(thread_t *td, const char *file, int line)
;   defined in mutex.c
extern _thread_unlock

; void proc_kill_tid(proc_t *proc, pid_t tid, int exit_code);
;   defined in proc.c
extern proc_kill_tid

; trapframe_restore
;   defined in exception.asm
extern trapframe_restore


section .data
FILENAME db __FILE__, 0


section .text
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; void switch_thread(thread_t *old_td, thread_t *new_td)
;
;  old_td->lock should be held on entry and is released once the state is saved
global switch_thread
switch_thread:
  xor al, al
  test rdi, rdi ; check if old thread is NULL (i.e. thread exit)
  jz .restore_thread ; if NULL just restore the new thread

  ; ========================================
  ;            save old thread
  ; ========================================

  mov r8, THREAD_TCB(rdi)
  mov rax, [rsp] ; return address
  mov TCB_RIP(r8), rax
  mov TCB_RSP(r8), rsp
  mov TCB_RBP(r8), rbp
  mov TCB_RBX(r8), rbx
  mov TCB_R12(r8), r12
  mov TCB_R13(r8), r13
  mov TCB_R14(r8), r14
  mov TCB_R15(r8), r15
  ; use the rflags that were preserved by critical_enter() which should
  ; include the interrupt flag, which at present is disabled
  mov r11, PERCPU_RFLAGS
  mov TCB_RFLAGS(r8), r11

;  ; update the thread's kernel stack pointer
;  mov qword THREAD_KSTACK_PTR(rdi), rsp

  ; ==== save base registers
  bt dword TCB_FLAGS(r8), TCB_KERNEL
  jc .done_save_base ; skip it for kernel threads

  ; save fsbase
  mov ecx, FSBASE_MSR
  rdmsr
  shl rdx, 32
  or rax, rdx
  mov TCB_FSBASE(r8), rax
  ; save kgsbase
  mov ecx, KGSBASE_MSR
  rdmsr
  shl rdx, 32
  or rax, rdx
  mov TCB_GSBASE(r8), rax
.done_save_base:

  ; ==== save debug registers
  bt dword TCB_FLAGS(r8), TCB_DEBUG
  jnc .done_save_debug ; skip it
  mov rax, dr7
  mov r11, dr0
  mov r12, dr1
  mov r13, dr2
  mov r14, dr3
  mov r15, dr6
  mov TCB_DR0(r8), r11
  mov TCB_DR1(r8), r12
  mov TCB_DR2(r8), r13
  mov TCB_DR3(r8), r14
  mov TCB_DR6(r8), r15
  mov TCB_DR7(r8), rax

  and rax, 0x0000FC00 ; disable breakpoints
  mov dr7, rax
.done_save_debug:

  ; ==== save fpu registers
  bt dword TCB_FLAGS(r8), TCB_FPU
  jnc .done_save_fpu
  mov r9, TCB_FPUSTATE(r8)
  fxsave [r9]
.done_save_fpu:

  ; up to this point interrupts have been disabled because we are in a critical section
  ; as long as the old thread lock is held. when we call _thread_unlock we will exit the
  ; critical section and our flags restored from PERCPU_RFLAGS. since we dont want to
  ; be interrupted while restoring the next thread we mask out the interrupt flag from
  ; the saved flags.
  and dword PERCPU_RFLAGS, ~0x0200 ; clear the interrupt flag

  ; done with current thread now release the thread lock
  ; rdi = old_td
  ; rsi = file
  ; rdx = line
  push rdi ; preserve old_td across the call
  push rsi ; preserve new_td across the call
  sub rsp, 8 ; align stack to 16 bytes
  mov rsi, FILENAME
  mov rdx, __LINE__
  call _thread_unlock
  add rsp, 8 ; restore stack alignment
  pop rsi
  pop rdi

  mov rax, THREAD_PROC(rdi)
  test rax, THREAD_PROC(rsi) ; ZF=0 if we need to switch address space
  setz al ; al = 0 if switch needed

  ; ========================================
  ;           restore new thread
  ; ========================================
  ;  rsi = next thread
  ;  al = switch cr3

.restore_thread:
  cmp al, 0 ; check if we need to switch cr3
  jnz .skip_cr3_switch ; al=1 if we can skip cr3 switch
  mov rax, THREAD_PROC(rsi)
  mov rax, PROCESS_SPACE(rax)
  mov rbx, ADDR_SPACE_PGTABLE(rax)
  mov cr3, rbx ; switch address space
  mov PERCPU_SPACE, rax ; update percpu address space pointer
.skip_cr3_switch:

  ; update curthread and curproc
  mov PERCPU_THREAD, rsi
  mov rax, THREAD_PROC(rsi)
  mov PERCPU_PROCESS, rax
  ; update percpu user_sp
  mov rax, THREAD_USTACK_PTR(rsi)
  mov PERCPU_USER_SP, rax

  ; update tss rsp0 to point at the top of the IRQ stack
  ; this ensures that any exception from userspace pushes to the IRQ stack
  mov rax, PERCPU_IRQ_STACK_TOP
  mov r8, PERCPU_TSS_RSP0_PTR
  mov [r8], rax

  mov r8, THREAD_TCB(rsi)
  ; ==== load base registers
  bt dword TCB_FLAGS(r8), TCB_KERNEL
  jc .skip_load_base ; skip load base for kernel threads
  ; load fsbase
  mov rax, TCB_FSBASE(r8)
  mov rdx, TCB_FSBASE(r8)
  shr rdx, 32
  mov ecx, FSBASE_MSR
  wrmsr
  ; load kgsbase
  mov ecx, KGSBASE_MSR
  mov eax, TCB_GSBASE(r8)
  mov rdx, TCB_GSBASE(r8)
  shr rdx, 32
  wrmsr
.skip_load_base:

  ; ==== dispatch pending signals
  bt dword THREAD_FLAGS2(rsi), TDF2_SIGPEND
  jnc .skip_handle_signals
  mov r14, THREAD_KSTACK_PTR(rsi) ; save current kstack_ptr
  mov rax, TCB_RSP(r8)            ; update it so we dont overwrite the stack of the original context
  mov THREAD_KSTACK_PTR(rsi), rax
  mov rsp, THREAD_KSTACK_PTR(rsi) ; switch to this updated kernel stack pointer
  mov r15, rsp                    ; save stack pointer
  and rsp, -16                    ; align stack to 16 bytes
  call signal_dispatch
  mov rsp, r15                    ; restore stack pointer
  ; restore rsi and r8 first
  mov rsi, PERCPU_THREAD
  mov r8, THREAD_TCB(rsi)
  mov THREAD_KSTACK_PTR(rsi), r14 ; restore the original kstack_ptr
.skip_handle_signals:

  ; ==== check if we should restore from trapframe
  bt dword THREAD_FLAGS2(rsi), TDF2_TRAPFRAME
  jnc .skip_trapframe_restore
  and dword THREAD_FLAGS2(rsi), ~(1 << TDF2_TRAPFRAME) ; clear the flag
  mov rsp, THREAD_FRAME(rsi) ; rsp = trapframe
  mov r8, PERCPU_THREAD      ; r8 = thread pointer
  jmp trapframe_restore
  ; unreachable
.skip_trapframe_restore:

  ; ==== load debug registers
  bt dword TCB_FLAGS(r8), TCB_DEBUG
  jnc .skip_load_debug ; skip it
  mov rax, dr7
  mov r11, TCB_DR0(r8)
  mov r12, TCB_DR1(r8)
  mov r13, TCB_DR2(r8)
  mov r14, TCB_DR3(r8)
  mov r15, TCB_DR6(r8)
  mov rcx, TCB_DR7(r8)
  and rax, 0x0000FC00 ; save reserved bits
  and rcx, ~0x0000FC00
  or rax, rcx
  mov dr7, rax
.skip_load_debug:

 ; ==== load fpu registers
  bt dword TCB_FLAGS(r8), TCB_FPU
  jnc .skip_load_fpu
  mov r9, TCB_FPUSTATE(r8)
  fxrstor [r9]
.skip_load_fpu:

  ; ==== load general registers
  mov rax, TCB_RIP(r8)
  mov rsp, TCB_RSP(r8)
  mov rbp, TCB_RBP(r8)
  mov rbx, TCB_RBX(r8)
  mov r12, TCB_R12(r8)
  mov r13, TCB_R13(r8)
  mov r14, TCB_R14(r8)
  mov r15, TCB_R15(r8)

  bt dword TCB_FLAGS(r8), TCB_SYSRET
  jc .do_sysret

  sub rsp, 8
  push qword TCB_RFLAGS(r8) ; save rflags
  popfq ; restore rflags
  mov [rsp], rax ; return address
  ret

.do_sysret:
  ; clear the sysret flag
  and dword TCB_FLAGS(r8), ~(1 << TCB_SYSRET)
  ; sysret loads rip from rcx and rflags from r11
  mov rcx, TCB_RIP(r8)
  mov r11, TCB_RFLAGS(r8)
  swapgs
  o64 sysret
; end sched_switch


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; void kernel_thread_entry()
;
global kernel_thread_entry
kernel_thread_entry:
  ; the stack should be set up like so:
  ;        <arg 5>
  ;          ...
  ;   +16  <arg 0>
  ;   +0   function rip
  ;  rsp
  ;
  pop rax ; pop the function address
  pop rdi ; pop arg 0
  pop rsi ; pop arg 1
  pop rdx ; pop arg 2
  pop rcx ; pop arg 3
  pop r8  ; pop arg 4
  pop r9  ; pop arg 5

  and rsp, -16  ; align rsp to 16 bytes
  call rax      ; call the function

  ; if the function returns we need to kill the thread (and/or process)
  mov rdi, PERCPU_THREAD
  mov rsi, THREAD_TID(rdi) ; rsi = tid
  mov rdi, PERCPU_PROCESS  ; rdi = proc
  mov rdx, rax             ; rdx = exit code
  call proc_kill_tid
  ; unreachable
