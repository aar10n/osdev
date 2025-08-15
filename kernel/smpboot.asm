;
; SMP Trampoline
;

%define SMPBOOT_START 0x1000
%define SMPDATA_START 0x2000

; struct smpboot offsets
%define SMP_INIT_ID     0x00
%define SMP_GATE        0x04
%define SMP_AP_ACK      0x08
%define SMP_ACK_BMP     0x0C
%define SMP_PML4        0x10
%define SMP_STACK       0x18
%define SMP_PERCPU      0x20
%define SMP_MAIN_TD     0x28
%define SMP_IDLE_TD     0x30
%define SMP_SPACE       0x38

; struct percpu offsets
%define PERCPU_SPACE        gs:0x10
%define PERCPU_THREAD       gs:0x18
%define PERCPU_SCRATCH_RAX  gs:0x60

%define CODE_SEGMENT 0x08
%define DATA_SEGMENT 0x10

%define GSBASE_MSR      0xC0000101
%define IA32_EFER_MSR   0xC0000080

%define label_rel(l) (SMPBOOT_START + (l - ap_boot))
%define data_ptr(ofst) (SMPDATA_START + (ofst))

extern ap_entry

; ------ smpboot start ------ ;
global smpboot_start
smpboot_start:

; ----------------- ;
;   16-Bit Code     ;
; ----------------- ;

; All APs are booted at the same time so we use a BSP controlled gate
; to select individual APs to boot up one-by-one.
bits 16
ap_boot:
  cli
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax

  ; load zero-length idt to force triple fault on NMI
  lidt [label_rel(idt_desc)]

  ; determine APIC id
  mov eax, 0x1
  cpuid
  mov cl, 24
  shr ebx, cl
  and ebx, 0xFF

  lock bts dword [data_ptr(SMP_ACK_BMP)], ebx ; set bit in ack bitmap
  mov eax, 0xDEADBEAF ; just a marker for debugging
  mov ebp, ebx

  ; ==== the APs synchronize here and wait for their turn to boot ====
.wait_for_turn:
  pause
  cmp word [data_ptr(SMP_INIT_ID)], bx
  jne .wait_for_turn
  ; selected AP now has exclusive access

  mov byte [data_ptr(SMP_AP_ACK)], 1 ; acknowledge to BSP
  mov ebx, 0xDEADBEAF ; just a marker for debugging
.wait_for_bsp: ; wait for bsp to release gate
  pause
  cmp byte [data_ptr(SMP_GATE)], 1
  je .wait_for_bsp
  ; AP has exclusive access and has been acknowledged by the BSP
  mov ecx, 0xDEADBEAF ; just a marker for debugging

  ;
  ; Prepare to enter long mode
  ;

  ; set PSE, PAE, PGE, OSFXSR and OSXMMEXCPT bits for SSE support
  mov eax, 0b11011110000
  mov cr4, eax

  ; load pml4 into cr3
  mov eax, [data_ptr(SMP_PML4)]
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

  mov rsp, [data_ptr(SMP_STACK)]      ; use ap kernel stack
  mov rax, [data_ptr(SMP_PERCPU)]     ; use ap per-cpu data
  mov rdx, rax
  mov cl, 32
  shr rdx, cl
  mov ecx, GSBASE_MSR
  wrmsr

  mov rax, [data_ptr(SMP_SPACE)]      ; set per-cpu address space
  mov PERCPU_SPACE, rax
  mov rax, [data_ptr(SMP_MAIN_TD)]    ; set per-cpu thread
  mov PERCPU_THREAD, rax
  mov rax, [data_ptr(SMP_IDLE_TD)]    ; HACK: pass the idle thread through the scratch register
  mov PERCPU_SCRATCH_RAX, rax

  ; finally close gate to signal BSP that we have booted
  mov byte [data_ptr(SMP_GATE)], 1
  mov byte [data_ptr(SMP_AP_ACK)], 0

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
