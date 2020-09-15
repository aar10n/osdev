KERNEL_CS equ 0x08
KERNEL_DS equ 0x10

;
; CPU
;

global cpuinfo
cpuinfo:
  mov edi, [esp + 4]  ; cpuinfo pointer
  mov eax, 0x1        ; 1 for cpu info
  cpuid
  mov [edi], eax      ; cpuinfo->eax
  mov [edi + 4], ebx  ; cpuinfo->ebx
  mov [edi + 8], ecx  ; cpuinfo->ecx
  mov [edi + 12], edx ; cpuinfo->edx
  ret

;
; Registets
;

; Instruction pointer
global get_eip
get_eip:
;  mov eax, [esp]
;  ret
  pop eax ; pop esi into eax
  jmp eax ; "return" without

; Stack pointer
global get_esp
get_esp:
  mov eax, esp
  ret

global set_esp
set_esp:
  mov esp, [esp + 4]
  ret

; Base pointer
global get_ebp
get_ebp:
  mov eax, ebp
  ret

global set_ebp
set_ebp:
  mov ebp, [esp + 4]
  ret

;
; I/O Ports
;

; output data to port - byte
;   @param - uint16_t port
;   @param - uint8_t data
global outb
outb:
  mov dx, [esp + 4] ; port
  mov al, [esp + 8] ; data
  out dx, al
  ret

; input data from port - byte
;   @param - uint16_t port
;   @returns - uint8_t
global inb
inb:
  mov dx, [esp + 4] ; port
  in  al, dx
  ret

; output data to port - word
;   @param - uint16_t port
;   @param - uint16_t data
global outw
outw:
  mov dx, [esp + 4] ; port
  mov ax, [esp + 8] ; data
  out dx, ax
  ret

; input data from port - word
;   @param - uint16_t port
;   @returns - uint16_t
global inw
inw:
  mov dx, [esp + 4] ; port
  in  ax, dx
  ret

; output data to port - double word
;   @param - uint16_t port
;   @param - uint32_t data
global outdw
outdw:
  mov dx, [esp + 4]  ; port
  mov eax, [esp + 8] ; data
  out dx, eax
  ret

; input data from port - double word
;   @param - uint16_t port
;   @returns - uint32_t
global indw
indw:
  mov dx, [esp + 4] ; port
  in eax, dx
  ret

;
; GDT/IDT
;

; loads the global descriptor table
;   @param - void *ldt
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
  ; long jump
  jmp KERNEL_CS:.flush
.flush:
  ret

; loads the interrupt descriptor table
;   @param - void *idt
global  load_idt
load_idt:
  mov eax, [esp + 4]
  lidt [eax]
  ret

;

; invalidates a page in the tlb
;   @param - int page
global invl_page
invl_page:
  mov eax, [esp + 4]
  invlpg [eax]

;
; Interrupts
;

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

global enable_interrupts
enable_interrupts:
  sti
  ret

global disable_interrupts
disable_interrupts:
  cli
  ret

;
; FPU
;

global has_fpu
fpu_exists: dw 0x0000
has_fpu:
  mov edx, cr0
  mov ebx, cr0
  btr ebx, 2  ; clear EM bit
  btr ebx, 3  ; clear TS bit

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

;
; SSE
;

global has_sse
has_sse:
  mov eax, 0x1
  cpuid
  test edx, 1 << 25
  jz nosse
  jmp hassse
  nosse:
  mov eax, 0
  ret
  hassse:
  mov eax, 1
  ret

global enable_sse
enable_sse:
  mov ebx, cr0
  and ebx, ~(1 << 2) ; clear the EM bit
  or ebx, 1 << 1     ; set the MP bit
  mov cr0, ebx

  mov ebx, cr4
  or ebx, 1 << 8     ; set the OSFXSR bit
  or ebx, 1 << 9     ; set the OSXMMEXCPT bit
  mov cr4, ebx
  ret
