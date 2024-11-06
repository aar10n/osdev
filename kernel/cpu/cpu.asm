%include "kernel/base.inc"

; Interrupts

global cpu_disable_interrupts
cpu_disable_interrupts:
  cli
  ret

global cpu_enable_interrupts
cpu_enable_interrupts:
  sti
  ret

global cpu_save_clear_interrupts
cpu_save_clear_interrupts:
  pushfq
  pop rax
  cli
  ret

global cpu_restore_interrupts
cpu_restore_interrupts:
  push rdi
  popfq
  ret

; General Registers

global cpu_read_stack_pointer
cpu_read_stack_pointer:
  mov rax, rsp
  ret

global cpu_write_stack_pointer
cpu_write_stack_pointer:
  mov rsp, rdi
  ret

global cpu_read_msr
cpu_read_msr:
  mov ecx, edi
  rdmsr

  shl rdx, 32
  or rax, rdx
  ret

global cpu_write_msr
cpu_write_msr:
  mov rax, rsi
  mov rdx, rsi
  shr rdx, 32

  mov ecx, edi
  wrmsr
  ret

global cpu_read_tsc
cpu_read_tsc:
  mov eax, 0x1
  cpuid
  rdtsc
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret

;

global cpu_read_fsbase
cpu_read_fsbase:
  mov rsi, rdi
  mov rdi, FS_BASE_MSR
  call cpu_read_msr
  ret

global cpu_write_fsbase
cpu_write_fsbase:
  mov rsi, rdi
  mov rdi, FS_BASE_MSR
  call cpu_write_msr
  ret

global cpu_read_gsbase
cpu_read_gsbase:
  mov rdi, GS_BASE_MSR
  call cpu_read_msr
  ret

global cpu_write_gsbase
cpu_write_gsbase:
  mov rsi, rdi
  mov rdi, GS_BASE_MSR
  call cpu_write_msr
  ret

global cpu_read_kernel_gsbase
cpu_read_kernel_gsbase:
  mov rdi, KERNEL_GS_BASE_MSR
  call cpu_read_msr
  ret

global cpu_write_kernel_gsbase
cpu_write_kernel_gsbase:
  mov rsi, rdi
  mov rdi, KERNEL_GS_BASE_MSR
  call cpu_write_msr
  ret

; GDT/IDT

global cpu_load_gdt
cpu_load_gdt:
  lgdt [rdi]
  ret

global cpu_load_idt
cpu_load_idt:
  lidt [rdi]
  ret

global cpu_load_tr
cpu_load_tr:
  ltr di
  ret

; void cpu_set_cs(uint16_t cs)
global cpu_set_cs
cpu_set_cs:
  lea rax, [rel .reload]
  push rdi
  push rax
  retfq ; pops IP followed by CS
.reload:
  ret

; void cpu_set_ds(uint16_t ds)
global cpu_set_ds
cpu_set_ds:
  mov ss, di
  mov ds, di
  mov es, di
  ret

; Control Registers

global __read_cr0
__read_cr0:
  mov rax, cr0
  ret

global __write_cr0
__write_cr0:
  mov cr0, rdi
  ret

global __read_cr2
__read_cr2:
  mov rax, cr2
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

global __fxsave
__fxsave:
  fxsave [rdi]
  ret

global __fxrstor
__fxrstor:
  fxrstor [rdi]
  ret

; Paging/TLB

global cpu_flush_tlb
cpu_flush_tlb:
  mov rax, cr3
  mov cr3, rax
  ret

; Syscalls

global syscall
syscall:
  mov rax, rdi ; code
  syscall

global sysret
sysret:
  mov KERNEL_SP, rsp
  mov rsp, USER_SP
  swapgs

  mov rcx, rdi ; rip
  mov rsp, rsi ; rsp
  mov r11, 0   ; rflags
  o64 sysret
