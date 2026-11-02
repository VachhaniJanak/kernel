#include <arch/x86_64/io.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <platform/attributes.h>
#include <stdint.h>
#include <utils/log.h>

WEAK void page_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  LOG_ERROR("Page Fault");
  LOG_ERROR("Address   : 0x%lx", page_fault_addr());
  LOG_ERROR("Error code: 0x%lx", frame->error_code);

  if (frame->error_code & 0x1) {
    LOG_ERROR("Page-level protection violation");
  } else {
    LOG_ERROR("Non-present page");
  }

  if (frame->error_code & 0x2) {
    LOG_ERROR("Write access");
  } else {
    LOG_ERROR("Read access");
  }

  if (frame->error_code & 0x4) {
    LOG_ERROR("User-mode access");
  } else {
    LOG_ERROR("Kernel-mode access");
  }

  while (1);
}

WEAK void gp_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  log_print("General Protection Fault:\n");
  log_print("  Error code: 0x%lx\n", frame->error_code);
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  log_print("  CS: 0x%lx\n", frame->cs);
  log_print("  SS: 0x%lx\n", frame->ss);
  while (1);
}

WEAK void invalid_tss_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  LOG_ERROR("TSS Fault\n");
  while (1);
}

WEAK void invalid_opcode_isr_handler(struct interrupt_frame_s* frame) {
  UNUSED(frame);
  log_error("Invalid Opcode Fault:");
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  log_print("  CS: 0x%lx\n", frame->cs);
  log_print("  SS: 0x%lx\n", frame->ss);
  while (1);
}

WEAK void double_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  log_error("Double Fault:");
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  while (1);
}

WEAK void timer_irq_isr_handler(struct scheduler_frame_s* frame) {
  UNUSED(frame);
}

WEAK void keyboard_irq_isr_handler(void) { volatile char scancode = inb(0x60); }

WEAK void ahci_irq_isr_handler(void) {}

WEAK void apic_timer_irq_isr_handler(void) {}

WEAK void simd_fpu_isr_handler(struct scheduler_frame_s* frame){
  UNUSED(frame);
  log_error("SIMD/FPU Exception:");
  // log_print("  RIP: 0x%lx\n", frame->rip);
  // log_print("  RSP: 0x%lx\n", frame->rsp);
  // log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  // log_print("  CS: 0x%lx\n", frame->cs);
  // log_print("  SS: 0x%lx\n", frame->ss);
  while (1);
}