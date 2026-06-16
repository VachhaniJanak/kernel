#include <stdint.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/interrupt.h>
#include <utils/log.h>

__attribute__((interrupt))
void PageFaultHandler(void *frame, uint64_t error_code) {
  LOG_INFO("Page Fault");
  LOG_INFO("Address   : 0x%lx", page_fault_addr());
  LOG_INFO("Error code: 0x%lx", error_code);
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
