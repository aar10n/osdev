ENTRY(_start)

kernel = 0x00100000;
offset = 0xC0000000;
kheap_base = offset + kernel;

SECTIONS
{
  _kernel_start = .;

  . = kheap_base;

  .text : AT(ADDR(.text) - offset) {
    *(.text)
    *(.rodata)
    . = ALIGN(0x1000);
  }

  .data : AT(ADDR(.data) - offset) {
    *(.data)
    . = ALIGN(0x1000);
  }

  .bss : AT(ADDR(.bss) - offset) {
    *(COMMON)
    *(.bss)
    . = ALIGN(0x1000);
  }

  _kernel_end = .;
}
