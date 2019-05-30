#include <stdint.h>

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t data);
uint16_t inw(uint16_t port);

void load_idt(void * idt);
void load_gdt(void * idt);
void interrupt();
void interrupt_out_of_memory();
void enable_hardware_interrupts();
void disable_hardware_interrupts();
