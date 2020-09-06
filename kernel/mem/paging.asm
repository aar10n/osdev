global enable_paging
enable_paging:
  mov ecx, cr0        ; Get cr0
  or ecx, (1 << 31)   ; Set paging bit high
  mov cr0, ecx        ; Set cr0

global disable_paging
disable_paging:
  mov ecx, cr0        ; Get cr0
  and ecx, ~(1 << 31) ; Set paging bit low
  mov cr0, ecx        ; Set cr0

global switch_page_directory
switch_page_directory:
  mov eax, [esp + 4]  ; Page directory address
  mov cr3, eax        ; Load the page directory
  call enable_paging  ; Force enable paging
  ret
