;
; SMP Trampoline
;
%include "base.inc"

%define label_rel(l) (SMPBOOT_START + (l - ap_boot))
%define data(ofst) (SMPDATA_START + (ofst))

extern ap_entry

; ------ smpboot start ------ ;
global smpboot_start
smpboot_start:

bits 16
ap_boot:
  ; 16-bit real mode
  lidt [label_rel(idt_desc)]     ; load null idt

  ; set PAE and PE bits
  mov eax, 0b10100000
  mov cr4, eax

  ; load pml4 into cr3
  mov eax, [data(SMP_PML4)]
  mov cr3, eax

  ; enable long mode
  mov ecx, IA32_EFER_MSR
  rdmsr
  or eax, (1 << 8)
  wrmsr

  ; set PE and PG bits
  mov eax, cr0
  or eax, 0x80000001
  mov cr0, eax

  lgdt [label_rel(gdt_desc)]     ; load gdt

  ; load cs with 64-bit segment and flush cache
  jmp KERNEL_CS:label_rel(ap_boot64)

bits 64
ap_boot64:
  ; 64-bit long mode
  mov byte [data(SMP_STATUS)], 1 ; signal sucess
  mov rsi, [data(SMP_STACK)]     ; kernel stack
  mov rax, ap_entry
  jmp rax                        ; jump to entry

;
; Data
;

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
