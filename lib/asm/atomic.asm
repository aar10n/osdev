;
; Atomic Functions
;

global __atomic_fetch_add64
__atomic_fetch_add64:
  mov rax, rsi
  lock xadd qword [rdi], rax
  ret

global __atomic_fetch_add32
__atomic_fetch_add32:
  mov eax, esi
  lock xadd dword [rdi], eax
  ret

global __atomic_fetch_add16
__atomic_fetch_add16:
  mov eax, esi
  lock xadd word [rdi], ax
  ret

global __atomic_fetch_add8
__atomic_fetch_add8:
  mov eax, esi
  lock xadd byte [rdi], al
  ret

global __atomic_bit_test_and_set
__atomic_bit_test_and_set:
  lock bts word [rdi]
  xor rax, rax
  adc rax, 0
  ret

global __atomic_bit_test_and_reset
__atomic_bit_test_and_reset:
  lock btr word [rdi]
  xor rax, rax
  adc rax, 0
  ret
