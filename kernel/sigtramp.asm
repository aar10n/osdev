;
; Signal Trampoline
;

; struct percpu offsets
%define PERCPU_THREAD         gs:0x18

; struct thread offsets
%define THREAD_KSTACK_PTR(x)  [x+0x98]
%define THREAD_USTACK_PTR(x)  [x+0xA0]

; struct sigframe offsets
%define SIGFRAME_INFO(x)      [x+0x00]
%define SIGFRAME_ACT(x)       [x+0x80]
%define SIGFRAME_CTX(x)       [x+0xA0]

; struct siginfo offsets
%define SIGINFO_SIGNO(x)      [x+0x00]

; struct sigaction offsets
%define SIGACT_HANDLER(x)     [x+0x00]
%define SIGACT_MASK(x)        [x+0x08]
%define SIGACT_FLAGS(x)       [x+0x88]
%define SIGACT_RESTORER(x)    [x+0x90]

; signal flags
%define SA_NOCLDSTOP  1
%define SA_NOCLDWAIT  2
%define SA_SIGINFO    4
%define SA_ONSTACK    0x08000000
%define SA_RESTART    0x10000000
%define SA_NODEFER    0x40000000
%define SA_RESETHAND  0x80000000
%define SA_RESTORER   0x04000000
%define SA_KERNHAND   0x02000000

; struct sigcontext offsets
%define SIGCTX_R8(x)          [x+0x00]
%define SIGCTX_R9(x)          [x+0x08]
%define SIGCTX_R10(x)         [x+0x10]
%define SIGCTX_R11(x)         [x+0x18]
%define SIGCTX_R12(x)         [x+0x20]
%define SIGCTX_R13(x)         [x+0x28]
%define SIGCTX_R14(x)         [x+0x30]
%define SIGCTX_R15(x)         [x+0x38]
%define SIGCTX_RDI(x)         [x+0x40]
%define SIGCTX_RSI(x)         [x+0x48]
%define SIGCTX_RBP(x)         [x+0x50]
%define SIGCTX_RBX(x)         [x+0x58]
%define SIGCTX_RDX(x)         [x+0x60]
%define SIGCTX_RAX(x)         [x+0x68]
%define SIGCTX_RCX(x)         [x+0x70]
%define SIGCTX_RSP(x)         [x+0x78]
%define SIGCTX_RIP(x)         [x+0x80]
%define SIGCTX_RFLAGS(x)      [x+0x88]
%define SIGCTX_CS(x)          [x+0x90]
%define SIGCTX_GS(x)          [x+0x92]
%define SIGCTX_FS(x)          [x+0x94]
%define SIGCTX_ERR(x)         [x+0x98]
%define SIGCTX_TRAPNO(x)      [x+0xA0]
%define SIGCTX_OLDMASK(x)     [x+0xA8]
%define SIGCTX_CR2(x)         [x+0xB0]
%define SIGCTX_FPSTATE(x)     [x+0xB8]

%define SIGFRAME_SIZE         416
%define SIGINFO_SIZE          128
%define SIGACT_SIZE           32

%define NR_rt_sigreturn       15


; make sure the trampoline is 4k aligned and occupies its own page
; so we can map it as user read/executeable
align 0x1000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; void sigtramp_entry(struct siginfo *info, const struct sigaction *act, uintptr_t rsp, bool user_mode)
;
;
global sigtramp_entry
sigtramp_entry:
  ; rdi = struct siginfo *info
  ; rsi = struct sigaction *act
  ; rdx = bool user_mode

  mov r8, PERCPU_THREAD ; r8 = current thread
  cmp rdx, 0 ; check if we are in kernel mode
  cmovz r9, THREAD_KSTACK_PTR(r8)
  cmovnz r9, THREAD_USTACK_PTR(r8)

  ; r9 contains the stack on which we will allocate the sigframe
  ; allocate a sigframe on the provided stack

  sub r9, SIGFRAME_SIZE ; r9 = sigframe pointer

  push rcx ; preserve user_mode
  push rdi ; preserve siginfo

  ; zero out the sigframe
  xor rax, rax                ; value (0)
  mov rdi, r9                 ; dest
  mov rcx, SIGFRAME_SIZE      ; size
  rep stosb

  ; copy sigaction into the sigframe
  lea rdi, SIGFRAME_ACT(r9)   ; dest
  ; rsi = act                 ; src
  mov rcx, SIGACT_SIZE        ; size
  rep movsb

  ; copy siginfo into the sigframe
  lea rdi, SIGFRAME_INFO(r9)  ; dest
  pop rsi ; siginfo           ; src
  mov rcx, SIGINFO_SIZE       ; size
  rep movsb

  ; ====== save context ======
  lea rdi, SIGFRAME_CTX(r9)   ; rdi = struct sigcontext *
  mov SIGCTX_R12(rdi), r12
  mov SIGCTX_R13(rdi), r13
  mov SIGCTX_R14(rdi), r14
  mov SIGCTX_R15(rdi), r15
  mov SIGCTX_RBP(rdi), rbp
  mov SIGCTX_RBX(rdi), rbx
  ; save rflags
  pushfq
  pop rsi
  mov SIGCTX_RFLAGS(rdi), rsi
  mov rcx, [rsp+0x08]         ; save return address
  mov SIGCTX_RIP(rdi), rcx
  mov SIGCTX_RSP(rdi), rsp    ; save return rsp
  add qword SIGCTX_RSP(rdi), 0x10 ; skip pushed rcx and return address

  ; setup trampoline arguments
  mov rdx, rdi                ; struct sigcontext *ctx (arg3)
  lea rsi, SIGFRAME_INFO(r9)  ; struct siginfo *info   (arg2)
  mov rdi, SIGINFO_SIGNO(rsi) ; int signo              (arg1)

  pop rcx                     ; pop user_mode

  ; before we switch jmp to the handler we need to save THREAD_KSTACK_PTR
  ; on the kernel stack (we we are currently on) and update it to rsp
  push qword THREAD_KSTACK_PTR(r8)
  mov THREAD_KSTACK_PTR(r8), rsp ; update thread's kernel stack pointer

  cmp rcx, 0                  ; check if we are in kernel mode
  jz .kernel_trampoline       ;   yes - jump to trampoline
                              ;   no - continue to sysret

  ; ====== sysret ======
  mov rcx, .trampoline        ; rip
  mov rsp, r9                 ; rsp = user stack pointer
  mov r11, 0x202              ; rflags
  swapgs
  o64 sysret

.kernel_trampoline:
  mov rsp, r9 ; update stack pointer to the sigframe

.trampoline:
  ; registers on entry:
  ;   rdi = int signo
  ;   rsi = struct siginfo *info
  ;   rdx = struct sigcontext *ctx

  ; ====== call handler ======
  ; handler(signo, info, ctx)
  ;   rdi = int signo
  ;   rsi = struct siginfo *info
  ;   rdx = struct sigcontext *ctx
  lea rax, SIGFRAME_ACT(rsp)  ; struct sigaction *act
  mov rax, SIGACT_HANDLER(rax)
  mov r15, rsp                ; save sigframe pointer
  and rsp, -16                ; align stack to 16 bytes
  call rax
  mov rsp, r15                ; restore sigframe pointer

  lea rax, SIGFRAME_ACT(rsp) ; struct sigaction *act
  mov rdi, SIGACT_FLAGS(rax)
  and rdi, SA_KERNHAND
  jnz sigreturn ; already in kernel mode

  ; rt_sigreturn
  mov rax, NR_rt_sigreturn
  syscall

global sigreturn
sigreturn:
  ; rsp should point to the bottom of the sigframe
  lea rsp, SIGFRAME_CTX(rsp) ; struct sigcontext *ctx

  ; ====== restore context ======
  mov r12, SIGCTX_R12(rsp)
  mov r13, SIGCTX_R13(rsp)
  mov r14, SIGCTX_R14(rsp)
  mov r15, SIGCTX_R15(rsp)
  mov rbp, SIGCTX_RBP(rsp)
  mov rbx, SIGCTX_RBX(rsp)
  mov r11, SIGCTX_RFLAGS(rsp)
  push r11
  popfq

  mov rax, SIGCTX_RIP(rsp)
  mov rsp, SIGCTX_RSP(rsp)
  jmp rax

; pad the rest of the page to ensure the trampoline occupies an exclusive page
times (0x1000 - ($ - sigtramp_entry)) db 0
