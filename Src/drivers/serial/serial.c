#include <arch/x86_64/io.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <stdarg.h>
#include <stdint.h>
#include <utils/printf.h>

static spinlock_t serial_lock;

void serial_init(void) {
  spinlock_init(&serial_lock);
  outb(0x3F8 + 1, 0x00);  // Disable interrupts
  outb(0x3F8 + 3, 0x80);  // Enable DLAB
  outb(0x3F8 + 0, 0x03);  // Set baud rate divisor to 3 (38400 baud)
  outb(0x3F8 + 1, 0x00);
  outb(0x3F8 + 3, 0x03);  // 8 bits, no parity, one stop bit
  outb(0x3F8 + 2, 0xC7);  // Enable FIFO
  outb(0x3F8 + 4, 0x0B);  // IRQs enabled, RTS/DSR set
}

static void _serial_putchar(char character, void* arg) {
  UNUSED(arg);
  while (!(inb(0x3F8 + 5) & 0x20));  // Wait for empty transmit buffer
  outb(0x3F8, character);
}

int serial_printf(const char* format, ...) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&serial_lock, flags);

  va_list va;
  va_start(va, format);

  int ret = vfctprintf(_serial_putchar, NULL, format, va);

  va_end(va);
  
  SPIN_LOCK_RELEASE(&serial_lock, flags);
  return ret;
}
