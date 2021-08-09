; Optimized assembly procedures


;
; Bit Test
;

; uint8_t __bt8(uint8_t byte, uint8_t bit)
global __bt8
__bt8:
  mov dx, word [rdi] ; get byte value
  bt dx, si          ; bit test
  setc al            ; get test result
  ret

; uint8_t __bt64(uint64_t qword, uint8_t bit)
global __bt64
__bt64:
  xor rax, rax         ; zero rax
  mov rdx, qword [rdi] ; get byte value
  bt rdx, rsi          ; bit test
  setc al              ; get test result
  ret

;
; Bit Test Set
;

; uint8_t __bts8(uint8_t *byte, uint8_t bit)
global __bts8
__bts8:
  xor eax, eax       ; zero eax
  mov dx, word [rdi] ; get byte value
  bts dx, si         ; bit test set
  mov [rdi], dx      ; set byte value
  setc al            ; get test result
  ret

; uint8_t __bts64(uint64_t *qword, uint8_t bit)
global __bts64
__bts64:
  xor rax, rax         ; zero rax
  mov rdx, qword [rdi] ; get qword value
  bts rdx, rsi         ; bit test set
  mov [rdi], rdx       ; set qword value
  setc al              ; get test result
  ret

;
; Bit Test Reset
;

; uint8_t __btr8(uint8_t *byte, uint8_t bit)
global __btr8
__btr8:
  xor eax, eax       ; zero eax
  mov dx, word [rdi] ; get byte value
  btr dx, si         ; bit test set
  mov [rdi], dx      ; set byte value
  setc al            ; get test result
  ret

; uint8_t __btr64(uint64_t *qword, uint8_t bit)
global __btr64
__btr64:
  xor rax, rax         ; zero rax
  mov rdx, qword [rdi] ; get qword value
  btr rdx, rsi         ; bit test set
  mov [rdi], rdx       ; set qword value
  setc al              ; get test result
  ret

;
; Bit Scan Forward
;

; uint8_t __bsf8(uint8_t byte)
global __bsf8
__bsf8:
  bsf ax, di ; bit scan forward
  ret

; uint8_t __bsf32(uint32_t dword)
global __bsf32
__bsf32:
  bsf rax, rdi ; bit scan forward
  ret

; uint8_t __bsf64(uint64_t qword)
global __bsf64
__bsf64:
  bsf rax, rdi ; bit scan forward
  ret

;
; Bit Scan Reverse
;

; uint8_t __bsr8(uint8_t byte)
global __bsr8
__bsr8:
  bsr ax, di ; bit scan forward
  ret

; uint8_t __bsr64(uint64_t qword)
global __bsr64
__bsr64:
  mov rdx, rdi ; get byte value
  bsr rax, rdx ; bit scan forward
  ret

;
; Population Count
;

; uint8_t __popcnt8(uint8_t byte)
global __popcnt8
__popcnt8:
  popcnt ax, di
  ret

; uint8_t __popcnt64(uint64_t qword)
global __popcnt64
__popcnt64:
  popcnt rax, rdi
  ret
