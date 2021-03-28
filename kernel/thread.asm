;
; Thread Related
;

%define NULL 0x00

; percpu offsets
%define PERCPU_ID       0x00
%define PERCPU_THREAD   0x08
%define PERCPU_PROCESS  0x10
%define CURRENT_THREAD  gs:PERCPU_THREAD
%define CURRENT_PROCESS gs:PERCPU_PROCESS

; process offsets
%define PROCESS_PID     0x00
%define PROCESS_PPID    0x04
%define PROCESS_VM      0x08

; thread offsets
%define THREAD_ID       0x00
%define THREAD_CTX      0x08
%define THREAD_PROCESS  0x10
%define THREAD_TLS_BLCK 0x18

; tls offsets
%define TLS_BASE_ADDR   0x00

; thread context offsets
%define CTX_RAX    0x00
%define CTX_RBX    0x08
%define CTX_RBP    0x10
%define CTX_R12    0x18
%define CTX_R13    0x20
%define CTX_R14    0x28
%define CTX_R15    0x30
;
%define CTX_RIP    0x38
%define CTX_CS     0x40
%define CTX_RFLAGS 0x48
%define CTX_RSP    0x50
%define CTX_SS     0x58

extern thread_entry
extern vm_swap_vmspace
extern write_fsbase


global thread_entry_stub
thread_entry_stub:
  pop qword rsi   ; routine argument
  pop qword rdi   ; routine pointer
  call thread_entry
  ; this should never return
  jmp $


; perform a thread context switch
; void thread_switch(thread_t *thread)
global thread_switch
thread_switch:
  ; rdi = next thread
  ; rsi = next process
  ; rdx = current thread
  ; rcx = current process
;  cli
  mov rsi, [rdi + THREAD_PROCESS]
  mov rdx, CURRENT_THREAD
  mov rcx, CURRENT_PROCESS

  ; only save context if thread is not null
  cmp rdx, NULL
  je .switch_thread

  ; return early if thread has not changed
  cmp rdx, rdi
  jne .save_ctx
  jmp .restore_ctx

.save_ctx: ; save old context
  pushfq
  mov rdx, [rdx + THREAD_CTX]

  mov [rdx + CTX_RAX], rax
  mov [rdx + CTX_RBX], rbx
  mov [rdx + CTX_RBP], rbp
  mov [rdx + CTX_R12], r12
  mov [rdx + CTX_R13], r13
  mov [rdx + CTX_R14], r14
  mov [rdx + CTX_R15], r15

  pop qword [rdx + CTX_RFLAGS] ; rflags
  pop qword [rdx + CTX_RIP]    ; rip
  mov [rdx + CTX_RSP], rsp     ; rsp

.switch_thread: ; update thread
  mov CURRENT_THREAD, rdi

  ; avoid msr access if thread doesnt use tls
  mov rax, [rdi + THREAD_TLS_BLCK]
  mov rax, [rax + TLS_BASE_ADDR]
  cmp rax, NULL
  je .switch_process

  ; update thread local storage pointer
  push rdi
  push rsi
  ; write tls base address
  mov rdi, rax
  call write_fsbase
  pop rsi
  pop rdi

.switch_process:
  ; if we're switching to a thread from the same process
  ; we dont have to update anything process related
  cmp rcx, rsi
  je .restore_ctx

  ; update current process
  mov CURRENT_PROCESS, rsi
  mov rsi, [rsi + PROCESS_VM]

  ; switch virtual memory
  push rdi
  mov rdi, rsi
  call vm_swap_vmspace
  pop rdi

.restore_ctx:
  ; restore new context
  mov rsp, [rdi + THREAD_CTX]

  pop rax
  pop rbx
  pop rbp
  pop r12
  pop r13
  pop r14
  pop r15

  sti
  iretq


; fast path to return to current thread
; void thread_continue()
global thread_continue
thread_continue:
  mov rdi, CURRENT_THREAD
  mov rsp, [rdi + THREAD_CTX]

  pop rax
  pop rbx
  pop rbp
  pop r12
  pop r13
  pop r14
  pop r15

  iretq
