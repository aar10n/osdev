; https://github.com/freebsd/freebsd-src/blob/main/sys/amd64/amd64/cpu_switch.S

; struct percpu offsets
%define PERCPU_THREAD         gs:0x18
%define PERCPU_PROCESS        gs:0x20
%define PERCPU_USER_SP        gs:0x40
%define PERCPU_KERNEL_SP      gs:0x48
%define PERCPU_TSS_RSP0_PTR   gs:0x50

; struct proc offsets
%define PROCESS_SPACE(x)      [x+0x08]

; struct thread offsets
%define THREAD_FLAGS(x)       [x+0x04]
%define THREAD_LOCK(x)        [x+0x08]
%define THREAD_TCB(x)         [x+0x20]
%define THREAD_PROCESS(x)     [x+0x28]
%define THREAD_FRAME(x)       [x+0x30]
%define THREAD_KSTACK_BASE(x) [x+0x38]
%define THREAD_KSTACK_SIZE(x) [x+0x40]

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
%define TCB_KGSBASE(x)        [x+0x50]
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
%define TCB_IRETQ   3 ; needs iretq
%define TCB_SYSRET  4 ; needs sysret

%define FSBASE_MSR  0xC0000100
%define GSBASE_MSR  0xC0000101
%define KGSBASE_MSR 0xC0000102

%define TCB_SIZE        0xa0 ; 0x98 aligned to 16 bytes
%define TRAPFRAME_SIZE  0xc0

; top of the thread stack sits below the trapframe and tcb
%define STACK_TOP_OFF   (TCB_SIZE + TRAPFRAME_SIZE)


; void switch_address_space(address_space_t *new_space)
extern switch_address_space
; void _thread_unlock(thread_t *td)
extern _thread_unlock

; defined in exception.asm
extern trapframe_restore


; void sched_do_switch(thread_t *old_td, thread_t *new_td)
global sched_do_switch
sched_do_switch:
  ; the old_td lock should be held on entry and will be released once the state is saved
  test qword rdi, 0
  jnz .switch_address_space ; no current thread to save

  ; ====================
  ;     save thread
  ; ====================

  ; ==== save registers
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
  pushfq
  pop r11
  mov TCB_RFLAGS(r8), r11

  ; ==== save base registers
  bt dword TCB_FLAGS(r8), TCB_KERNEL
  jc .done_save_base ; skip it for kernel threads

  push rdx ; -- save lock
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
  mov TCB_KGSBASE(r8), rax
  pop rdx ; -- restore lock
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

  mov rax, THREAD_PROCESS(rdi)
  push rsi
  ; done with the current thread now release the thread lock
  call _thread_unlock ; rdi = curr
  pop rsi

  test rax, THREAD_PROCESS(rsi)
  ; if the next thread is from the same process
  ; we dont need to switch the address space
  jz .done_switch_address_space

  ; switch_address_space(next->space)
.switch_address_space:
  mov rax, THREAD_PROCESS(rsi)
  mov r12, rsi ; next -> r12
  mov rdi, PROCESS_SPACE(rax) ; next->space
  call switch_address_space
  mov rsi, r12 ; next -> rsi
.done_switch_address_space:

  ; ====================
  ;    restore thread
  ; ====================

.restore_thread:
  ; update curthread and curproc
  mov PERCPU_THREAD, rsi
  mov rax, THREAD_PROCESS(rsi)
  mov PERCPU_PROCESS, rax
  ; update percpu kernel_sp
  mov rax, THREAD_KSTACK_BASE(rsi)
  add rax, THREAD_KSTACK_SIZE(rsi)
  sub rax, STACK_TOP_OFF
  mov PERCPU_KERNEL_SP, rax
  mov qword PERCPU_USER_SP, 0
  ; update tss rsp0 to point at the top of the thread trapframe
  mov rax, THREAD_FRAME(rsi)
  add rax, TRAPFRAME_SIZE
  mov r8, PERCPU_TSS_RSP0_PTR
  mov [r8], rax

  mov r8, THREAD_TCB(rsi)
  ; ==== load base registers
  bt dword TCB_FLAGS(r8), TCB_KERNEL
  jc .done_load_base ; skip load base for kernel threads
  ; load fsbase
  mov rax, TCB_FSBASE(r8)
  mov rdx, TCB_FSBASE(r8)
  shr rdx, 32
  mov ecx, FSBASE_MSR
  wrmsr
  ; load kgsbase
  mov ecx, KGSBASE_MSR
  mov eax, TCB_KGSBASE(r8)
  mov rdx, TCB_KGSBASE(r8)
  shr rdx, 32
  wrmsr
.done_load_base:

  ; ==== load debug registers
  bt dword TCB_FLAGS(r8), TCB_DEBUG
  jnc .done_load_debug ; skip it
  mov rax, dr7
  mov r11, TCB_DR0(r8)
  mov r12, TCB_DR1(r8)
  mov r13, TCB_DR2(r8)
  mov r14, TCB_DR3(r8)
  mov r15, TCB_DR6(r8)
  mov rcx, TCB_DR7(r8)
  and rax, 0x0000FC00 ; preserve reserved bits
  and rcx, ~0x0000FC00
  or rax, rcx
  mov dr7, rax
.done_load_debug:

 ; ==== load fpu registers
  bt dword TCB_FLAGS(r8), TCB_FPU
  jnc .done_load_fpu
  mov r9, TCB_FPUSTATE(r8)
  fxrstor [r9]
.done_load_fpu:

  ; ==== load registers
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

  bt dword TCB_FLAGS(r8), TCB_IRETQ
  jc .do_iretq

  sub rsp, 8
  mov [rsp], rax ; return address
  ret

.do_sysret:
  and dword TCB_FLAGS(r8), ~TCB_SYSRET
  ; sysret loads rip from rcx and rflags from r11
  mov rcx, TCB_RIP(r8)
  mov r11, TCB_RFLAGS(r8)
  swapgs
  o64 sysret

.do_iretq:
  ; TODO: fix this
  and dword TCB_FLAGS(r8), ~TCB_IRETQ
  jmp trapframe_restore
; end sched_switch
