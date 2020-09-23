ENTRY(start)

kernel = 0x00100000;
offset = 0xC0000000;

SECTIONS {
  . = kernel + offset;

  kernel_image_start = .;
  .text : AT(ADDR(.text) - offset) {
    *(.multiboot)
    kernel_text_start = .;
    *(.text)
    kernel_text_end = .;
  }
  .rodata ALIGN(4K) : AT(ADDR(.rodata) - offset) {
    *(.rodata)
  }
  .data ALIGN(4K) : AT(ADDR(.data) - offset) {
    kernel_data_start = .;
    *(.data)
    kernel_data_end = .;
  }
  .bss : AT(ADDR(.bss) - offset) {
    kernel_bss_start = .;
    *(COMMON)
    *(.bss)
    kernel_bss_end = .;
  }
  kernel_image_end = .;
}
