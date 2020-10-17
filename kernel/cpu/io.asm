;
; Related I/O and Register Functions
;

; output data to port - byte
global outb
outb:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  mov eax, esi ; data
  out dx, al   ; output

  pop rbp
  ret

; input data from port - byte
global inb
inb:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  in  al, dx   ; input

  pop rbp
  ret

; output data to port - word
global outw
outw:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  mov eax, esi ; data
  out dx, ax   ; output

  pop rbp
  ret

; input data from port - word
global inw
inw:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  in  ax, dx   ; input

  pop rbp
  ret

; output data to port - double word
global outdw
outdw:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  mov eax, esi ; data
  out dx, eax  ; output

  pop rbp
  ret

; input data from port - double word
global indw
indw:
  push rbp
  mov rbp, rsp

  mov edx, edi ; port
  in eax, dx   ; input

  pop rbp
  ret
