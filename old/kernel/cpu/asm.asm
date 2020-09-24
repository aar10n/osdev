KERNEL_CS equ 0x08
KERNEL_DS equ 0x10

;
; Registers
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

; Base pointer
global get_ebp
get_ebp:
  mov eax, ebp
  ret

;void get_msr(uint32_t msr, uint64_t *value) {
;  __asm volatile("rdmsr" : "=a"(*lo), "=q"(*hi) : "c"(msr));
;}
;
;void set_msr(uint32_t msr, uint64_t value) {
;  __asm volatile("wrmsr" : : "a"(lo), "q"(hi), "c"(msr));
;}
global get_msr
get_msr:
  mov ecx, [esp + 4] ; msr address
  mov edi, [esp + 8] ; uint64_t pointer
  rdmsr
  mov [edi], eax     ; low 32 bits
  mov [edi + 4], edx ; high 32 bits
  ret


global set_msr
set_msr:
  mov ecx, [esp + 4]  ; msr address
  mov eax, [esp + 8]  ; low 32 bits of value
  mov edx, [esp + 12] ; high 32 bits of value
  wrmsr
  ret

;
; I/O Ports
;

; output data to port - byte
global outb
outb:
  mov dx, [esp + 4] ; port
  mov al, [esp + 8] ; data
  out dx, al
  ret

; input data from port - byte
global inb
inb:
  mov dx, [esp + 4] ; port
  in  al, dx
  ret

; output data to port - word
global outw
outw:
  mov dx, [esp + 4] ; port
  mov ax, [esp + 8] ; data
  out dx, ax
  ret

; input data from port - word
global inw
inw:
  mov dx, [esp + 4] ; port
  in  ax, dx
  ret

; output data to port - double word
global outdw
outdw:
  mov dx, [esp + 4]  ; port
  mov eax, [esp + 8] ; data
  out dx, eax
  ret

; input data from port - double word
global indw
indw:
  mov dx, [esp + 4] ; port
  in eax, dx
  ret

;
; GDT/IDT
;

; loads the global descriptor table
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
global  load_idt
load_idt:
  mov eax, [esp + 4]
  lidt [eax]
  ret

;
; Interrupts
;

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
