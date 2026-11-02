#include <arch/cpu.h>
#include <arch/x86_64/x86_64.h>

void cpu_init(void) {
  fpu_hardware_init();
}