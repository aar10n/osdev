include(CMakeForceCompiler)
cmake_minimum_required(VERSION 3.14)

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i386)
set(CMAKE_C_COMPILER i386-elf-gcc)
set(CMAKE_LINKER i386-elf-ld)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-g -ffreestanding -Wall -Wextra -m32")

set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <INCLUDES> <FLAGS> -c <SOURCE> -o <OBJECT>")
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -f elf32 <SOURCE> -o <OBJECT>")
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> -T ${CMAKE_SOURCE_DIR}/linker.ld <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

# --------- #
#  Project  #
# --------- #

project(osdev)
enable_language(C ASM ASM_NASM)

include_directories(include)

# --------- #
#  Targets  #
# --------- #

add_library(drivers OBJECT
    drivers/asm.asm include/drivers/asm.h
    drivers/screen.c include/drivers/screen.h
    drivers/keyboard.c include/drivers/keyboard.h
    drivers/serial.c include/drivers/serial.h
    drivers/ata.c include/drivers/ata.h
    drivers/rtc.c include/drivers/rtc.h)

add_library(fs OBJECT
    fs/ext2/ext2.c include/fs/ext2/ext2.h
    fs/ext2/superblock.c include/fs/ext2/superblock.h
    fs/ext2/inode.c include/fs/ext2/inode.h
    fs/ext2/directory.c include/fs/ext2/directory.h)

add_library(libc OBJECT
    libc/stdlib.c include/stdlib.h
    libc/string.c include/string.h
    libc/stdio.c include/stdio.h libc/math.c include/math.h)

add_executable(kernel
    boot.asm
    kernel/main.c
    kernel/cpu/interrupt.asm
    kernel/cpu/idt.c kernel/cpu/idt.h
    kernel/cpu/isr.c include/kernel/cpu/isr.h
    kernel/cpu/timer.c include/kernel/cpu/timer.h
    kernel/cpu/gdt.c include/kernel/cpu/gdt.h
    include/multiboot.h
    kernel/mem/mem.c kernel/mem/mem.h
    kernel/mem/alloc.c kernel/mem/alloc.h kernel/mem/page.c kernel/mem/page.h)
target_link_libraries(kernel drivers fs libc)
