ENTRY(_start)

kernel = 0x00100000;
offset = 0xC0000000;

SECTIONS {
  . = kernel + offset;
  _kernel_start = .;

  .multiboot : AT(ADDR(.multiboot) - offset) {
    *(.multiboot)
  }
  .text : AT(ADDR(.text) - offset) {
    *(.text)
  }
  .rodata ALIGN(4K) : AT(ADDR(.rodata) - offset) {
    *(.rodata*)
  }
  .data ALIGN(4K) : AT(ADDR(.data) - offset) {
    *(.data)
  }
  .bss ALIGN(4K) : AT(ADDR(.bss) - offset) {
    *(COMMON)
    *(.bss)
  }

  _kernel_end = .;
}
