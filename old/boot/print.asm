; putc - Print a single character
putc:
  pusha
  mov ah, 0x0E
  int 0x10
  popa
  ret

; puts - Print a string
puts:
  pusha          ; Save the registers
  .start:
    mov al, [bx]   ; Get the character at `bx`
    cmp al, 0      ; Loop until char is null
    je .end
    call putc      ; Otherwise print the char
    inc bx         ; Move one character over
    jmp .start
  .end:
    popa           ; Restore the registers
    ret            ; Return


; putx - Print a hex value
putx:
  pusha            ; Save the registers
  mov cl, 0        ; Setup loop counter
  .start:
    cmp cl, 4        ; Loop 4 times
    je .end
    mov ax, dx       ; Move the value into `ax`
    and ax, 0xF      ; Single out the prev bit
    add al, '0'      ; Convert it to ASCII
    cmp al, '9'      ; Check if `al` is less than 9
    jle .skip
    add al, 0x7      ; Add aditional offset for A-F
  .skip:
    mov bx, .hex + 5 ; Move .hex into `bx` offset by 5
    sub bx, cx       ; Move `bx` over by the `cx` offset
    mov [bx], al     ; Move the ASCII value into `bx`
    ror dx, 4        ; Rotate `dx` to get the next digit
    inc cl           ; Increment the counter
    jmp .start
  .end:
    mov bx, .hex     ; Move the final string into `bx`
    call puts        ; Print the string
    popa             ; Restore the registers
    ret              ; Return

.hex: db '0x0000', 0 ;
