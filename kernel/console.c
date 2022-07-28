//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#include <console.h>
#include <spinlock.h>
#include <mutex.h>
#include <drivers/serial.h>

console_t *kconsole = NULL;


// static inline void aquire_kheap() {
//   if (PERCPU_THREAD == NULL) {
//     spin_lock(&kheap_lock);
//   } else {
//     mutex_lock(&kheap_mutex);
//   }
// }
//
// static inline void release_kheap() {
//   if (PERCPU_THREAD == NULL) {
//     spin_unlock(&kheap_lock);
//   } else {
//     mutex_unlock(&kheap_mutex);
//   }
// }

void kputs(const char *s) {
  if (kconsole) {
    kconsole->puts(kconsole->ptr, s);
  }
}

void kputc(char c) {
  if (kconsole) {
    kconsole->putc(kconsole->ptr, c);
  }
}

char kgetc() {
  if (kconsole) {
    return kconsole->getc(kconsole->ptr);
  }
  return 0;
}

//

void early_console_puts(void *ptr, const char *s) {
  spinlock_t *lock = ptr;
  spin_lock(lock);
  serial_write(COM1, s);
  spin_unlock(lock);
}

void early_console_putc(void *ptr, char c) {
  spinlock_t *lock = ptr;
  spin_lock(lock);
  serial_write_char(COM1, c);
  spin_unlock(lock);
}

spinlock_t early_console_lock;
console_t early_console = {
  .ptr = NULL,
  .puts = early_console_puts,
  .putc = early_console_putc,
  .getc = NULL,
};

void console_early_init() {
  spin_init(&early_console_lock);
  early_console.ptr = &early_console_lock;
  serial_init(COM1);
  kconsole = &early_console;
}
