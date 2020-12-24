//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#include <syscall.h>
#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>
#include <vectors.h>
#include <stdio.h>

extern void syscall_handler();
extern gdt_entry_t gdt[];


static int exit(int ret) {
  kprintf(">>>> exit %d <<<<\n", ret);
  return 0;
}

//

int handle_syscall(int syscall, uint64_t arg) {
  if (syscall != 0) {
    return -1;
  }
  kprintf(">>> syscall %d <<<\n", syscall);
  return exit(arg);
}

void syscalls_init() {
  write_msr(IA32_LSTAR_MSR, (uintptr_t) syscall_handler);
  write_msr(IA32_SFMASK_MSR, 0);
  write_msr(IA32_STAR_MSR, 0x10LL << 48 | KERNEL_CS << 32);
}
