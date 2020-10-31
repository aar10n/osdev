;
; I/O Port Functions
;

; output data to port - byte
global outb
outb:
  mov edx, edi ; port
  mov eax, esi ; data
  out dx, al   ; output
  ret

; input data from port - byte
global inb
inb:
  mov edx, edi ; port
  in  al, dx   ; input
  ret

; output data to port - word
global outw
outw:
  mov edx, edi ; port
  mov eax, esi ; data
  out dx, ax   ; output
  ret

; input data from port - word
global inw
inw:
  mov edx, edi ; port
  in  ax, dx   ; input
  ret

; output data to port - double word
global outdw
outdw:
  mov edx, edi ; port
  mov eax, esi ; data
  out dx, eax  ; output
  ret

; input data from port - double word
global indw
indw:
  mov edx, edi ; port
  in eax, dx   ; input
  ret
