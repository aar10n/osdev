; Tasking related code


global perform_task_switch
extern switch_page_directory
perform_task_switch:
  cli
  mov ecx, [esp + 4]  ; new eip
  mov eax, [esp + 8]  ; new esp
  mov ebx, [esp + 12] ; new ebp
  mov edx, [esp + 16] ; page directory
  ; set new values
  mov esp, eax
  mov ebp, ebx
  mov cr3, edx
  mov eax, 0x12345
  sti
  jmp ecx

