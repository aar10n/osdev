
%define PROGRAM 0x10000
%define DATA 0x11000
%define label_rel(l) (PROGRAM + (l - user_test))

global user_start
user_start:

user_test:
  mov dword [DATA], 0xDEADBEEF
  push dword 0xDEADBEEF
  mov eax, 1
  mov ebx, 0
  syscall

.hang:
  pause
  jmp .hang

global user_end
user_end:
