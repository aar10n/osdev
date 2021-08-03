set disassembly-flavor intel
add-symbol-file build/hello.elf
add-symbol-file build/kernel.elf
b page_fault_handler
