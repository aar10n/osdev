ENTRY(entry)

kernel = 0xFFFFFFFF80000000;

SECTIONS {
  . = kernel;

  kernel_image_start = .;
  .text : AT(ADDR(.text) - kernel) {
    *(.text)
  }
  .rodata ALIGN(4K) : AT(ADDR(.rodata) - kernel) {
    *(.rodata)
  }
  .data ALIGN(4K) : AT(ADDR(.data) - kernel) {
    *(.data)
  }
  .eh_frame ALIGN(4K) : AT(ADDR(.eh_frame) - kernel) {
    *(.eh_frame)
  }
  .bss : AT(ADDR(.bss) - kernel) {
    *(.bss)
  }
  kernel_image_end = .;
}
