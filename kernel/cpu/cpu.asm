global disable_interrupts
disable_interrupts:
  cli
  ret

global enable_interrupts
enable_interrupts:
  sti

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
