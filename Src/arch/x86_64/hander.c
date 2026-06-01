#include <stdint.h>

#include <utils/log.h>

void PageFaultHandler(void) {
  LOG_INFO("Page Fault\n");
  while (1)
    ;
}

void InvalidTSSHandler(void) {
  LOG_INFO("TSS Fault\n");
  while (1)
    ;
}

void InvalidOpcodeHandler(void) {
  LOG_INFO("Invalid Opcode\n");
  while (1)
    ;
}

void DoubleFaultHandler(void) {
  LOG_INFO("Double Fault\n");
  while (1)
    ;
}

void GeneralProtectionFaultHandler(void) {
  LOG_INFO("General Protection Fault\n");
  while (1)
    ;
}

void TimerIRQHandler(void) {
  LOG_INFO("Timer IRQ");
  // while (1)
    // ;
  return;
}
