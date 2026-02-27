;
; Signal Trampoline
;
; User-mode trampoline page mapped as user-accessible. signal_dispatch()
; modifies the trapframe to jump here with sigframe on the user stack.

%define NR_rt_sigreturn       15

; struct sigframe offsets
%define SIGFRAME_ACT(x)       [x+0x80]

; struct sigaction offsets
%define SIGACT_HANDLER(x)     [x+0x00]


; make sure the trampoline is 4k aligned and occupies its own page
; so we can map it as user read/executeable
align 0x1000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; sigtramp_trampoline
;
; entry:
;   rdi = signo
;   rsi = &sigframe.info
;   rdx = &sigframe.ctx
;   rsp = sigframe pointer
;
global sigtramp_trampoline
sigtramp_trampoline:
  lea rax, SIGFRAME_ACT(rsp)
  mov rax, SIGACT_HANDLER(rax)
  mov r15, rsp              ; save sigframe pointer
  and rsp, -16              ; 16-byte align for call
  call rax                  ; handler(signo, info, ctx)
  mov rsp, r15              ; restore sigframe pointer
  mov rax, NR_rt_sigreturn
  syscall
  ; unreachable

; pad the rest of the page to ensure the trampoline occupies an exclusive page
times (0x1000 - ($ - sigtramp_trampoline)) db 0
