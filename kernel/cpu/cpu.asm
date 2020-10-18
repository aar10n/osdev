global cli
cli:
  cli
  ret

global sti
sti:
  sti
  ret

global read_tsc
read_tsc:
  rdtsc
  mov cl, 32
  shl rdx, cl
  or rax, rdx
  ret


; cpuid: eax = 1
global get_cpu_info
get_cpu_info:
  mov eax, 0x1        ; 1 for cpu info
  cpuid
  mov [rdi], eax      ; cpuinfo->eax
  mov [rdi + 4], ebx  ; cpuinfo->ebx
  mov [rdi + 8], ecx  ; cpuinfo->ecx
  mov [rdi + 12], edx ; cpuinfo->edx
  ret

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

  mov ax, 0x10
  mov ds, ax
  mov ax, 0x00
  mov es, ax
  mov fs, ax
  mov gs, ax

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

; TLB

global tlb_invlpg
tlb_invlpg:
  invlpg [rdi]
  ret

global tlb_flush
tlb_flush:
  mov rax, cr3
  mov cr3, rax
  ret
