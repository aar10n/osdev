ENTRY(entry)



SECTIONS {
  __kernel_address = 0x100000;
  __kernel_virtual_offset = 0xFFFF800000000000;
  . = __kernel_address + __kernel_virtual_offset;


  /* Kernel Code */
  __kernel_code_start = .;
  .text : AT(ADDR(.text)) {
    *(.text)
  }
  .rodata ALIGN(4K) : AT(ADDR(.rodata)) {
    *(.rodata)
  }
  .eh_frame : AT(ADDR(.eh_frame)) {
    *(.eh_frame)
  }
  . = ALIGN(4K);
  __kernel_code_end = .;

  /* Kernel Data */
  .data : AT(ADDR(.data)) {
    *(.data)
  }
  .boot_data : AT(ADDR(.boot_data)) {
    *(.boot_data)
  }
  .bss ALIGN(4K) : AT(ADDR(.bss)) {
    *(.bss)
  }
  . = ALIGN(4K);
  __kernel_data_end = .;

}
