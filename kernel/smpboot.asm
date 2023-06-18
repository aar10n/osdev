;
; SMP Trampoline
;
%include "kernel/base.inc"

%define SMPBOOT_START 0x1000
%define SMPDATA_START 0x2000

; Smpboot Data
%define SMP_LOCK    0x00
%define SMP_GATE    0x02
%define SMP_CPU_ID  0x04
%define SMP_COUNT   0x06
%define SMP_PML4    0x08
%define SMP_STACK   0x10
%define SMP_PERCPU  0x18

%define CODE_SEGMENT 0x08
%define DATA_SEGMENT 0x10

%define label_rel(l) (SMPBOOT_START + (l - ap_boot))
%define data(ofst) (SMPDATA_START + (ofst))

extern ap_entry

; ------ smpboot start ------ ;
global smpboot_start
smpboot_start:

; ----------------- ;
;   16-Bit Code     ;
; ----------------- ;

; All APs are booted at the same time so we must
; add a lock and gate to properly initialize them
; one-by-one.
bits 16
ap_boot:
  ; load zero-length idt to force triple fault on NMI
  lidt [label_rel(idt_desc)]

  ; determine APIC id
  mov eax, 0x1
  cpuid
  mov cl, 24
  shr ebx, cl
  and ebx, 0xFF

  lock add word [data(SMP_COUNT)], 1 ; increment ap count
  xor ax, ax

.aquire_lock:
  pause
  lock bts word [data(SMP_LOCK)], 0
  setc al
  cmp al, 0x00
  jne .aquire_lock
  ; ========= Lock Aquired =========

  mov byte [data(SMP_CPU_ID)], bl ; set current id

  ; wait for bsp to release gate
.wait_for_bsp:
  pause
  cmp word [data(SMP_GATE)], 1
  je .wait_for_bsp

  ;
  ; Prepare to enter long mode
  ;

  ; set PSE, PAE and PGE bits
  mov eax, 0b10110000
  mov cr4, eax

  ; load pml4 into cr3
  mov eax, [data(SMP_PML4)]
  mov cr3, eax

  ; set LME bit
  mov ecx, IA32_EFER_MSR
  rdmsr
  or eax, 0x100
  wrmsr

  ; activate long mode by enabling paging and protection
  mov eax, cr0
  and eax, 0x9fffffff ; clear CD and NW bits
  or eax, 0x80000001  ; set PG and PE bits
  mov cr0, eax

  ; load entry gdt
  lgdt [label_rel(gdt_desc)]

  ; load cs with 64-bit segment and flush cache
  jmp CODE_SEGMENT:label_rel(ap_boot64)

; ----------------- ;
;   64-Bit Code     ;
; ----------------- ;

bits 64
ap_boot64:
  mov ax, DATA_SEGMENT
  mov ss, ax
  mov ax, 0x00
  mov ds, ax
  mov es, ax

  mov rsp, [data(SMP_STACK)]      ; use ap kernel stack
  mov rax, [data(SMP_PERCPU)]     ; use ap per-cpu data
  mov rdx, rax
  mov cl, 32
  shr rdx, cl
  mov ecx, GS_BASE_MSR
  wrmsr

  ; finally close gate and release lock
  mov byte [data(SMP_GATE)], 1
  mov byte [data(SMP_LOCK)], 0

  ; ========= Lock Released =========
  mov rax, ap_entry
  jmp rax                         ; jump to entry

; ----------------- ;
;  Trampoline Data  ;
; ----------------- ;

; temporary gdt
align 4
gdt:
.gdt_null:
    dq 0x0000000000000000 ; null descriptor
.gdt_code:
    dq 0x00209A0000000000 ; 64-bit code descriptor (exec/read)
.gdt_data:
    dq 0x0000920000000000 ; 64-bit data descriptor (read/write)

align 4
gdt_desc:
    dw $ - gdt - 1    ; 16-bit size
    dd label_rel(gdt) ; 32-bit base

; temporary idt
align 4
idt_desc:
  dw  0 ; ignored
  dq  0 ; ignored

; ------ smpboot end ------ ;
global smpboot_end
smpboot_end:
