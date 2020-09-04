KERNEL_CS equ 0x08
KERNEL_DS equ 0x10

global outb
outb:
  mov al, [esp + 8] ; data
  mov dx, [esp + 4] ; port
  out dx, al
  ret

global inb
inb:
  mov dx, [esp + 4] ; port
  in  al, dx
  ret

global outw
outw:
  mov ax, [esp + 8] ; data
  mov dx, [esp + 4] ; port
  out dx, ax
  ret

global inw
inw:
  mov dx, [esp + 4] ; port
  in  ax, dx
  ret

global outdw
outdw:
  mov eax, [esp + 8] ; data
  mov dx, [esp + 4]  ; port
  out dx, eax
  ret

global indw
indw:
  mov dx, [esp + 4] ; port
  in eax, dx
  ret

; GDT/IDT

global load_gdt
load_gdt:
  mov eax, [esp + 4]
  lgdt [eax]
  mov ax, KERNEL_DS
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  jmp KERNEL_CS:.flush
.flush:
  ret

global  load_idt
load_idt:
  mov eax, [esp + 4]
  lidt [eax]
  ret

;

global invl_page
invl_page:
  mov eax, [esp + 4]
  invlpg [eax]

; Interrupts

global interrupt
interrupt:
  push ebp
  mov ebp, esp
  int 49
  pop ebp
  ret

global interrupt_out_of_memory
interrupt_out_of_memory:
  push ebp
  mov ebp, esp
  int 50
  pop ebp
  ret

global enable_hardware_interrupts
enable_hardware_interrupts:
  sti
  ret

global disable_hardware_interrupts
disable_hardware_interrupts:
  cli
  ret

; FPU

global has_fpu
fpu_exists: dw 0x0000
has_fpu:
  mov edx, cr0
  mov ebx, cr0
  btr ebx, 2  ; clear cr0.em
  btr ebx, 3  ; clear cr0.ts

  and edx, ebx
  mov cr0, edx
  fninit
  fnstsw [fpu_exists]
  cmp word [fpu_exists], 0
  jne nofpu
  jmp hasfpu
  nofpu:
  mov eax, 0
  ret
  hasfpu:
  mov eax, 1
  ret

