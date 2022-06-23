%include "base.inc"

; Interrupts

global __disable_interrupts
__disable_interrupts:
  cli
  ret

global __enable_interrupts
__enable_interrupts:
  sti
  ret

global __save_clear_interrupts
__save_clear_interrupts:
  pushfq
  pop rax
  ret

global __restore_interrupts
__restore_interrupts:
  push rdi
  popfq
  ret

global cli
cli:
  cli
  ret

global sti
sti:
  sti
  ret

global cli_save
cli_save:
  pushfq
  pop rax
  cli
  ret

global sti_restore
sti_restore:
  push rdi
  popfq
  ret

; Registers

global __read_tsc
__read_tsc:
  mov eax, 0x1
  cpuid
  rdtsc
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret

global __read_tscp
__read_tscp:
  mfence
  rdtscp
  lfence
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret

global read_tsc
read_tsc:
  rdtsc
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret


global __read_msr
__read_msr:
  mov ecx, edi
  rdmsr

  shl rdx, 32
  or rax, rdx
  ret

global __write_msr
__write_msr:
  mov rax, rsi
  mov rdx, rsi
  shr rdx, 32

  mov ecx, edi
  wrmsr
  ret

global read_msr
read_msr:
  mov ecx, edi
  rdmsr

  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret

global write_msr
write_msr:
  mov rax, rsi
  mov rdx, rsi
  mov cl, 32
  shr rdx, cl

  mov ecx, edi
  wrmsr
  ret


global read_fsbase
read_fsbase:
  mov rdi, FS_BASE_MSR
  call read_msr
  ret

global write_fsbase
write_fsbase:
  mov rsi, rdi
  mov rdi, FS_BASE_MSR
  call write_msr
  ret

global __write_fsbase
__write_fsbase:
  mov rsi, rdi
  mov rdi, FS_BASE_MSR
  call write_msr
  ret

global read_gsbase
read_gsbase:
  mov rdi, GS_BASE_MSR
  call read_msr
  ret

global write_gsbase
write_gsbase:
  mov rsi, rdi
  mov rdi, GS_BASE_MSR
  call write_msr
  ret

global read_kernel_gsbase
read_kernel_gsbase:
  mov rdi, KERNEL_GS_BASE_MSR
  call read_msr
  ret

global write_kernel_gsbase
write_kernel_gsbase:
  mov rsi, rdi
  mov rdi, KERNEL_GS_BASE_MSR
  call write_msr
  ret

global swapgs
swapgs:
  swapgs
  ret

; GDT/IDT

global load_gdt
load_gdt:
  lgdt [rdi]
  ret

global load_idt
load_idt:
  lidt [rdi]
  ret

global load_tr
load_tr:
  ltr di
  ret

global flush_gdt
flush_gdt:
  push rbp
  mov rbp, rsp

  mov ax, 0x00
  mov ds, ax
  mov es, ax

  lea rax, [rel .flush]

  ; set up the stack frame so we can call
  ; iretq to set our new cs register value
  push qword 0x10 ; new ss
  push rbp        ; rsp
  pushfq          ; flags
  push qword 0x08 ; new cs
  push rax        ; rip
  iretq
.flush:
  pop rbp

; Control Registers

global read_cr0
read_cr0:
  mov rax, cr0
  ret

global write_cr0
write_cr0:
  mov cr0, rdi
  ret

global __read_cr0
__read_cr0:
  mov rax, cr0
  ret

global __write_cr0
__write_cr0:
  mov cr0, rdi
  ret

global __read_cr3
__read_cr3:
  mov rax, cr3
  ret

global __write_cr3
__write_cr3:
  mov cr3, rdi
  ret

global __read_cr4
__read_cr4:
  mov rax, cr4
  ret

global __write_cr4
__write_cr4:
  mov cr4, rdi
  ret

global __xgetbv
__xgetbv:
  mov ecx, edi
  xgetbv
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret

global __xsetbv
__xsetbv:
  mov ecx, edi
  mov rax, rsi
  mov rdx, rsi
  mov cl, 32
  shr rdx, cl
  xsetbv
  ret

; Paging/TLB

global __flush_tlb
__flush_tlb:
  mov rax, cr3
  mov cr3, rax
  ret

global tlb_invlpg
tlb_invlpg:
  invlpg [rdi]
  ret

global tlb_flush
tlb_flush:
  mov rax, cr3
  mov cr3, rax
  ret

; SSE

global enable_sse
enable_sse:
  mov rdx, cr0
  and rdx, ~(1 << 2) ; clear the EM bit
  or rdx, 1 << 1     ; set the MP bit
  mov cr0, rdx

  mov rdx, cr4
  or rdx, 1 << 8     ; set the OSFXSR bit
  or rdx, 1 << 9     ; set the OSXMMEXCPT bit
  mov cr4, rdx
  ret

; Syscalls

global syscall
syscall:
  mov rax, rdi ; code
  syscall

global sysret
sysret:
  mov [KERNEL_SP], rsp
  mov rsp, [USER_SP]
  swapgs

  mov rcx, rdi ; rip
  mov rsp, rsi ; rsp
  mov r11, 0   ; rflags
  o64 sysret
