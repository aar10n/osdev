;
; util.asm
;   Contains a number of utility macros to make
;   working with the utility functions easier
; 

; print - Prints a string
%macro print 1
  mov bx, %1
  call puts
%endmacro

; println - Prints a string and a newline
%macro println 1
  mov bx, %1
  call puts
  mov al, 0xa
  call putc
  mov al, 0xd
  call putc
%endmacro

; print_hex - Prints a hex value
%macro print_hex 1
  mov dx, %1
  call putx
%endmacro

; println_hex - Prints a hex value and a newline
%macro println_hex 1
  mov dx, %1
  call putx
  mov al, 0xa
  call putc
  mov al, 0xd
  call putc
%endmacro
