; Flushes the translation lookaside buffer
global flush_tlb
flush_tlb:
  mov eax, cr3
  mov cr3, eax
  ret

; Enables paging
global enable_paging
enable_paging:
  mov ecx, cr0        ; set control reg
  or ecx, (1 << 31)   ; set paging bit high
  mov cr0, ecx        ; set control reg

; Disables paging
global disable_paging
disable_paging:
  mov ecx, cr0        ; get control reg
  and ecx, ~(1 << 31) ; set paging bit low
  mov cr0, ecx        ; set control reg

; Switches the page directory
;   @param - page_directory_t *pd
global switch_page_directory
extern current_directory
switch_page_directory:
  mov eax, [esp + 4]           ; page directory address
  mov cr3, eax                 ; load the page directory
  mov [current_directory], eax ; set the current directory
  call enable_paging           ; force enable paging
  ret

; Copies the data from one page frame to another
;   @param - uintptr_t src
;   @param - uintptr_t dest
global copy_page_frame
copy_page_frame:
  mov esi, [esp + 4]   ; source frame address
  mov edi, [esp + 8]   ; destination frame address

  cli                  ; disable interrupts
  call disable_paging  ; disable paging

  mov ecx, 1024        ; set count to 1024
  mov ebx, 0           ; set offset to 0
l1:
  mov eax, [esi + ebx] ; copy value from src
  mov [edi + ebx], eax ; copy value to dest
  add ebx, 4           ; offset to next byte
  dec ecx              ; decrease count by 1
  jnz l1

  call enable_paging   ; re-enable paging
  ret
