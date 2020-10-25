;
;
; Common Assembly Macros
;
;

; General Constants
%define KERNEL_OFFSET 0xFFFFFF8000000000
%define KERNEL_CS 0x08

%define APIC_BASE_VA 0xFFFFFFFFF8000000
%define APIC_REG_ID  0x20
%define APIC_REG_EOI 0x0B0

%define COM1 0x3F8

; Percpu Constants
%define PERCPU_ID 0x0
%define PERCPU_CURRENT 0x8
%define current gs:PERCPU_CURRENT

; Process Constants
%define PROC_PID 0x0
%define PROC_CTX 0x8

; Process Context
%define CTX_RBX    0x00
%define CTX_RBP    0x08
%define CTX_R12    0x10
%define CTX_R13    0x18
%define CTX_R14    0x20
%define CTX_R15    0x28

%define CTX_RIP    0x30
%define CTX_CS     0x38
%define CTX_RFLAGS 0x40
%define CTX_RSP    0x48
%define CTX_SS     0x50


; Saved registers
%macro pushall 0
  push r15
  push r14
  push r13
  push r12
  push r11
  push r10
  push r9
  push r8
  push rbp
  push rsi
  push rdi
  push rdx
  push rcx
  push rbx
  push rax
%endmacro

%macro popall 0
  pop rax
  pop rbx
  pop rcx
  pop rdx
  pop rdi
  pop rsi
  pop rbp
  pop r8
  pop r9
  pop r10
  pop r11
  pop r12
  pop r13
  pop r14
  pop r15
%endmacro

%macro pushcaller 0
  push r11
  push r10
  push r9
  push r8
  push rsi
  push rdi
  push rdx
  push rcx
  push rax
%endmacro

%macro popcaller 0
  pop rax
  pop rcx
  pop rdx
  pop rdi
  pop rsi
  pop r8
  pop r9
  pop r10
  pop r11
%endmacro

%macro pushcallee 0
  push r15
  push r14
  push r13
  push r12
  push rbp
  push rbx
%endmacro

%macro popcallee 0
  pop rbx
  pop rbp
  pop r12
  pop r13
  pop r14
  pop r15
%endmacro

%macro retz 0
  jnz %%skip
  ret
%%skip:
%endmacro

;
; Arrays
;

; Defines the beginning of a new array. This
; must be used on the same line as the label
; which will point to the final array. The
; array must be terminated with `endarray`.
%macro array 0
  %push @array_ctx
  %undef @array
  %define @array_name %00
  %assign @array_length 0
%endmacro

; Defines the end of an array. This assigns
; the label used with the `array` macro to
; the contents of the array. This also adds
; another label with the same name as the
; array with the added `_length` prefix,
; which the length of the array.
%macro endarray 0
  %pop @array_ctx
  @array_name: @array
;  %[@array_name]_length: dd @array_length
  %[@array_name]_length equ @array_length
  %undef @array
  %undef @array_name
  %undef @array_length
%endmacro

; Defines an array element that points to some
; arbitrary data. The pointer is then added to
; the containing array.
%macro item 1 ; item value
  %%item: %1
  %ifctx @array_ctx
    %ifdef @array
      %xdefine @array @array, %%item
    %else
      %xdefine @array dq %%item
    %endif
    %assign @array_length @array_length + 1
  %else
    %error "`item` must be used inside an array"
  %endif
%endmacro

; Defines a null-terminated string and in the
; context of an array, adds the string to the
; containing array.
%macro string 1+ ; string value
  %ifctx @array_ctx
    item {db %1, 0}
  %else
    db %1, 0
  %endif
%endmacro

;
; Calling C Code
;

%macro ccall 1 ; alignment
  %push @ccall_ctx
  %assign @ccall_arg_index 0
  %assign @ccall_stack_align %1
%endmacro

%macro endccall 0
  %pop @ccall_ctx
%endmacro

%macro @dest 1+
  %define .@reg_0 rdi
  %define .@reg_1 rsi
  %define .@reg_2 rdx
  %define .@reg_3 rcx
  %define .@reg_4 r8
  %define .@reg_5 r9
  %define .@reg(x) .@reg_ %+ x

  %ifctx @ccall_ctx
    %if @ccall_arg_index < 6
      %00 .@reg(@ccall_arg_index) %1
    %else
      %assign .@offset ((@ccall_arg_index - 6) * @ccall_stack_align)
      %00 [rsp + .@offset] %1
    %endif
    %assign @ccall_arg_index @ccall_arg_index + 1
  %else
    %error "`@dest` must be used within a c call"
  %endif
%endmacro
