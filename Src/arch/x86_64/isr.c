#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <platform/attributes.h>
#include <stdint.h>
#include <utils/log.h>

WEAK void page_fault_isr_handler(struct interrupt_frame_s* frame) {
  LOG_ERROR("Page Fault");
  LOG_ERROR("Address   : 0x%lx", page_fault_addr());
  LOG_ERROR("Error code: 0x%lx", frame->error_code);
  while (1);
}

WEAK void invalid_tss_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  LOG_ERROR("TSS Fault\n");
  while (1);
}

WEAK void invalid_opcode_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  LOG_ERROR("Invalid Opcode\n");
  while (1);
}

WEAK void double_fault_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  LOG_ERROR("Double Fault\n");
  while (1);
}

WEAK void gp_fault_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  LOG_ERROR("General Protection Fault\n");
  while (1);
}

WEAK void timer_irq_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  // LOG_ERROR("Timer IRQ");
  // while (1)
  // ;
}
