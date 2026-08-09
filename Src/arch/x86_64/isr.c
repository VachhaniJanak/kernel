#include <arch/x86_64/apic.h>
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
  LOG_ERROR("General Protection Fault");

  LOG_ERROR("Error code: 0x%lx", frame->error_code);

  if (frame->error_code & 0x1) {
    LOG_ERROR("External event");
  } else {
    LOG_ERROR("Internal event");
  }

  if (frame->error_code & 0x2) {
    LOG_ERROR("Descriptor location: GDT");
  } else {
    LOG_ERROR("Descriptor location: IDT");
  }

  if (frame->error_code & 0x4) {
    LOG_ERROR("Descriptor location: LDT");
  } else {
    LOG_ERROR("Descriptor location: IDT");
  }

  if (frame->error_code & 0x8) {
    LOG_ERROR("Descriptor location: IDT");
  } else {
    LOG_ERROR("Descriptor location: IDT");
  }

  if (frame->error_code & 0x10) {
    LOG_ERROR("Descriptor location: IDT");
  } else {
    LOG_ERROR("Descriptor location: IDT");
  }
  while (1);
}

WEAK void invalid_tss_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  LOG_ERROR("TSS Fault\n");
  while (1);
}

WEAK void invalid_opcode_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  LOG_ERROR("Invalid Opcode\n");
  while (1);
}

WEAK void double_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  UNUSED(frame);
  LOG_ERROR("Double Fault\n");
  while (1);
}

WEAK uint64_t timer_irq_isr_handler(struct scheduler_frame_s* frame) {
  UNUSED(frame);
  lapic_eoi();
}

WEAK void keyboard_irq_isr_handler(void) {
  volatile int scancode = inb(0x60);
  lapic_eoi();
}

WEAK void ahci_irq_isr_handler(void) { lapic_eoi(); }