;
; Processor related functions
;

;global has_cpuid
;has_cpuid:


; get operating frequency algorithm
;   1. Execute the CPUID instruction with an input value of EAX=0 and ensure the vendor-ID string returned is “GenuineIntel”
;   2. Execute the CPUID instruction with EAX=1 to load the EDX register with the feature flags.
;   3. Ensure that the TSC feature flag (EDX bit 4) is set. This indicates the processor supports the Time Stamp Counter and
;      RDTSC instruction.
;   4. Read the TSC at the beginning of the reference period
;   5. Read the TSC at the end of the reference period
;   6. Compute the TSC delta from the beginning and ending of the reference period.
;   7. Compute the actual frequency by dividing the TSC delta by the reference period.
;
;   Actual Frequency = (end tsc - start tsc) / reference period
;


; cpuid: eax = 1
global get_cpu_info
get_cpu_info:
  mov edi, [esp + 4]  ; cpuinfo pointer
  mov eax, 0x1        ; 1 for cpu info
  cpuid
  mov [edi], eax      ; cpuinfo->eax
  mov [edi + 4], ebx  ; cpuinfo->ebx
  mov [edi + 8], ecx  ; cpuinfo->ecx
  mov [edi + 12], edx ; cpuinfo->edx
  ret

global has_long_mode
has_long_mode:
  ; check if long mode identification
  ; function exists with cpuid
  mov eax, 0x80000000
  cpuid
  cmp eax, 0x80000001
  jb .no_long
  ; use long mode identification
  ; function
  mov eax, 0x80000001
  cpuid
  test edx, (1 << 29) ; check LM-bit
  jz .no_long
.has_long:
  mov eax, 1
  ret
.no_long:
  mov eax, 1
  ret
