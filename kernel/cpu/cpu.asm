FSBASE_MSR        equ 0xC0000100
GSBASE_MSR        equ 0xC0000101
KERNEL_GSBASE_MSR equ 0xC0000102

; Interrupts

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

global read_tsc
read_tsc:
  rdtsc
  mov cl, 32
  shl rdx, cl
  or rax, rdx
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
  mov rdi, FSBASE_MSR
  call read_msr
  ret

global write_fsbase
write_fsbase:
  mov rsi, rdi
  mov rdi, FSBASE_MSR
  call write_msr
  ret

global read_gsbase
read_gsbase:
  mov rdi, GSBASE_MSR
  call read_msr
  ret

global write_gsbase
write_gsbase:
  mov rsi, rdi
  mov rdi, GSBASE_MSR
  call write_msr
  ret

global read_kernel_gsbase
read_kernel_gsbase:
  mov rdi, KERNEL_GSBASE_MSR
  call read_msr
  ret

global write_kernel_gsbase
write_kernel_gsbase:
  mov rsi, rdi
  mov rdi, KERNEL_GSBASE_MSR
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

; Paging

global read_cr3
read_cr3:
  mov rax, cr3
  ret

global write_cr3
write_cr3:
  mov cr3, rdi
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
