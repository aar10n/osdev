ENTRY(entry)

kernel = 0x100000;
offset = 0xFFFFFF8000000000;

SECTIONS {
  . = kernel + offset;

  kernel_image_start = .;
  .text : AT(ADDR(.text)) {
    *(.text)
  }
  .rodata ALIGN(4K) : AT(ADDR(.rodata)) {
    *(.rodata)
  }
  .data ALIGN(4K) : AT(ADDR(.data)) {
    *(.data)
  }
  .bss : AT(ADDR(.bss)) {
    *(.bss)
  }
  .eh_frame ALIGN(4K) : AT(ADDR(.eh_frame)) {
    *(.eh_frame)
  }
  kernel_image_end = .;
}
