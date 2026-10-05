#include <arch/x86_64/interrupt.h>
#include <stdarg.h>
#include <utils/log.h>
#include <utils/panic.h>
#include <utils/printf.h>

void kernel_panic(const char* format, ...) {
  DISABLE_INT;

  static char buf[512];

  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  log_print(ANSI_COLOR_RED
            "\n*** KERNEL PANIC ***\n%s\nSystem halted.\n" ANSI_RESET,
            buf);

  for (;;) {
    __asm__ volatile("cli; hlt");
  }

  __builtin_unreachable();
}