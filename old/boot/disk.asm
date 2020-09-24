; disk_load - Load `dh` sectors to `es:bx` from drive `dl`
disk_load:
  pusha
  push dx

  mov ah, 0x02 ; BIOS read sector
  mov al, dh   ; Read `dl` sectors

  ; From the point
  mov ch, 0x00 ; Cylinder 0
  mov dh, 0x00 ; Head 0

  int 0x13       ; Call interrupt
  jc .disk_error ; A disk error occured

  pop dx
  cmp al, dh
  jne .sectors_error ; A sector error occured
  popa
  ret

  ; LBA = (( C x HPC ) + H ) x SPT + S - 1
  ; LBA = (( 0 * 16) + 16 ) * 63 + 10 - 1

  .disk_error:
    println DISK_READ_ERROR
    print ERROR_CODE
    
    mov dx, 0
    mov dl, al
    println_hex dx
    jmp $
  .sectors_error:
    println SECTORS_ERROR
    jmp $

DISK_READ_ERROR: db 'Disk read error', 0
SECTORS_ERROR: db 'Incorrect number of sectors read', 0
ERROR_CODE: db 'Error code: ', 0
