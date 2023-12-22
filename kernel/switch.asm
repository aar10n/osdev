; https://github.com/freebsd/freebsd-src/blob/main/sys/amd64/amd64/cpu_switch.S

; struct per_cpu offsets
%define PERCPU_THREAD       gs:0x10
%define PERCPU_PROCESS      gs:0x18

; struct process offsets
%define PROCESS_SPACE(x)    [x+0x8]

; struct thread offsets
%define THREAD_FLAGS(x)     [x+0x04]
%define THREAD_TCB(x)       [x+0x08]
%define THREAD_PROCESS(x)   [x+0x10]

; struct tcb offsets
%define TCB_RIP(x)          [x+0x00]
%define TCB_RSP(x)          [x+0x08]
%define TCB_RBP(x)          [x+0x10]
%define TCB_RBX(x)          [x+0x18]
%define TCB_R12(x)          [x+0x20]
%define TCB_R13(x)          [x+0x28]
%define TCB_R14(x)          [x+0x30]
%define TCB_R15(x)          [x+0x38]
%define TCB_FSBASE(x)       [x+0x40]
%define TCB_KGSBASE(x)      [x+0x48]
%define TCB_DR0(x)          [x+0x50]
%define TCB_DR1(x)          [x+0x58]
%define TCB_DR2(x)          [x+0x60]
%define TCB_DR3(x)          [x+0x68]
%define TCB_DR6(x)          [x+0x70]
%define TCB_DR7(x)          [x+0x78]
%define TCB_FPUSTATE(x)     [x+0x80]
%define TCB_FLAGS(x)        [x+0x88]

; TCB flags
%define TCB_KERNEL  0x01 ; kernel context
%define TCB_FPU     0x02 ; save fpu registers
%define TCB_DEBUG   0x04 ; save debug registers
%define TCB_IRETQ   0x08 ; needs iretq

%define FSBASE_MSR  0xC0000100
%define GSBASE_MSR  0xC0000101
%define KGSBASE_MSR 0xC0000102

; void switch_address_space(address_space_t *new_space);
extern switch_address_space

; int mutex_unlock(mutex_t *mutex);
extern mutex_unlock


; sched_switch(thread_t *curr, thread_t *next, mutex_t *lock);
;   rdi = current thread
;   rsi = next thread
;   rdx = current lock
global sched_switch
sched_switch:
  test qword rdi, 0
  jz .restore_thread ; no current thread to save

  ; ====================
  ;     save thread
  ; ====================

.save_thread:
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

  ; ==== save base registers
  test dword TCB_FLAGS(r8), TCB_KERNEL
  jz .done_save_base ; skip it for kernel threads
  ;   - fsbase
  rdfsbase rax
  mov TCB_FSBASE(r8), rax
  ;   - kgsbase
  push rdx ; save lock ptr
  mov ecx, KGSBASE_MSR
  rdmsr
  shl rdx, 32
  or rax, rdx
  mov TCB_KGSBASE(r8), rax
  pop rdx
.done_save_base:

  ; ==== save debug registers
  test dword TCB_FLAGS(r8), TCB_DEBUG
  jnz .done_save_debug ; skip it
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
  test dword TCB_FLAGS(r8), TCB_FPU
  jnz .done_save_fpu
  mov r9, TCB_FPUSTATE(r8)
  fxsave [r9]
.done_save_fpu:

  ; ==== switch address space
  test qword PERCPU_PROCESS, THREAD_PROCESS(r12)
  ; if the next thread is from the same process
  ; we dont need to switch the address space
  jz .done_switch_address_space

  ; switch_address_space(next->space)
  mov r12, rsi ; next -> r12
  mov r13, rdi ; curr -> r13
  mov rdi, PROCESS_VM_SPACE(r13) ; next->space
  call switch_address_space
  mov rsi, r12 ; next -> rsi
.done_switch_address_space:

  ; done with the current thread now release the lock
  mov rdi, rdx ; lock -> rdi
  call mutex_unlock

  ; ====================
  ;    restore thread
  ; ====================

.restore_thread:
  mov r8, THREAD_TCB(rsi)

  ; ==== load base registers
  test dword TCB_FLAGS(r8), TCB_KERNEL
  jz .done_load_base ; skip load base for kernel threads
  ;   - fsbase
  mov rax, TCB_FSBASE(r8)
  wrfsbase rax
  ;   - kgsbase
  mov ecx, KGSBASE_MSR
  mov eax, TCB_KGSBASE(r8)
  mov rdx, TCB_KGSBASE(r8)
  shr rdx, 32
  wrmsr
.done_load_base:

  ; ==== load debug registers
  test dword TCB_FLAGS(r8), TCB_DEBUG
  jnz .done_load_debug ; skip it
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
  test dword TCB_FLAGS(r8), TCB_FPU
  jnz .done_load_fpu
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

  mov [rsp], rax ; return address
  ret
; end sched_switch
