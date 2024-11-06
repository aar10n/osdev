; struct percpu offsets
%define PERCPU_THREAD         gs:0x18
%define PERCPU_PROCESS        gs:0x20
%define PERCPU_USER_SP        gs:0x40
%define PERCPU_KERNEL_SP      gs:0x48
%define PERCPU_TSS_RSP0_PTR   gs:0x50
%define PERCPU_SCRATCH_RAX    gs:0x60

; struct thread offsets
%define THREAD_FLAGS(x)       [x+0x04]
%define THREAD_LOCK(x)        [x+0x08]
%define THREAD_TCB(x)         [x+0x20]
%define THREAD_PROCESS(x)     [x+0x28]
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

; void handle_syscall(uint64_t syscall, struct trapframe *frame)
extern handle_syscall


global syscall_handler:
syscall_handler:
  swapgs

  ; swap to the kernel stack
  mov PERCPU_USER_SP, rsp
  mov rsp, PERCPU_KERNEL_SP

  ; preserve rax and free it up
  mov PERCPU_SCRATCH_RAX, rax

  ; setup a trapframe
  sub rsp, TRAPFRAME_SIZE
  mov qword TRAPFRAME_SS(rsp), 0
  mov rax, PERCPU_USER_SP
  mov TRAPFRAME_RSP(rsp), rax
  mov TRAPFRAME_RFLAGS(rsp), r11
  mov TRAPFRAME_RIP(rsp), rcx

  ; linux syscall abi
  ;   rax   syscall number
  ;   rcx   return address
  ;   r11   saved rflags
  ;   rdi   arg1
  ;   rsi   arg2
  ;   rdx   arg3
  ;   r10   arg4
  ;   r8    arg5
  ;   r9    arg6
  mov rax, PERCPU_SCRATCH_RAX
  mov TRAPFRAME_RAX(rsp), rax ; syscall number
  mov TRAPFRAME_RDI(rsp), rdi ; arg1
  mov TRAPFRAME_RSI(rsp), rsi ; arg2
  mov TRAPFRAME_RDX(rsp), rdx ; arg3
  mov TRAPFRAME_R10(rsp), r10 ; arg4
  mov TRAPFRAME_R8(rsp), r8   ; arg5
  mov TRAPFRAME_R9(rsp), r9   ; arg6

  mov TRAPFRAME_RBX(rsp), rbx
  mov TRAPFRAME_RBP(rsp), rbp
  mov TRAPFRAME_R12(rsp), r12
  mov TRAPFRAME_R13(rsp), r13
  mov TRAPFRAME_R14(rsp), r14
  mov TRAPFRAME_R15(rsp), r15

  ; systemv abi
  ;   rdi (syscall number)
  ;   rsi (trapframe)
  mov rdi, rax
  mov rsi, rsp
  call handle_syscall

  ; restore trapframe
  mov r15, TRAPFRAME_R15(rsp)
  mov r14, TRAPFRAME_R14(rsp)
  mov r13, TRAPFRAME_R13(rsp)
  mov r12, TRAPFRAME_R12(rsp)
  mov rbp, TRAPFRAME_RBP(rsp)
  mov rbx, TRAPFRAME_RBX(rsp)
  mov r9, TRAPFRAME_R9(rsp)
  mov r8, TRAPFRAME_R8(rsp)
  mov r10, TRAPFRAME_R10(rsp)
  mov rdx, TRAPFRAME_RDX(rsp)
  mov rsi, TRAPFRAME_RSI(rsp)
  mov rdi, TRAPFRAME_RDI(rsp)

  mov r11, TRAPFRAME_RFLAGS(rsp)
  mov rcx, TRAPFRAME_RIP(rsp)

  ; swap back to the user stack
  mov rsp, PERCPU_USER_SP
  swapgs
  o64 sysret
