#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <stddef.h>
#include <stdint.h>

IDTEntryTypedef idt[256] __attribute__((aligned(16))) = {0};

void set_idt_entry(size_t vector, void (*handler)(void), uint8_t ist,
                   uint8_t g_type, uint8_t dpl) {
  uintptr_t addr = (uintptr_t)handler;

  idt[vector].selector = 0x08;  // 0-1 RPL=0, 2 TI=0, 3-15 Index=1
  idt[vector].offset_low = 0xffff & addr;
  idt[vector].offset_mid = 0xffff & (addr >> 16);
  idt[vector].offset_high = 0xffffffff & (addr >> 32);
  idt[vector].reserved = 0;
  idt[vector].ist_zero = 0x7 & ist;
  idt[vector].attributes = 0;
  idt[vector].attributes = (0xf & g_type) | ((0x3 & dpl) << 5) | (1 << 7);
}

static inline void set_lidt(void* ptr) {
  __asm__ volatile("lidt (%0)" : : "r"(ptr) : "memory");
}

void init_idt(void) {
  IDTRTypedef idt_ptr = {.limit = sizeof(idt) - 1, .base = (uint64_t)idt};

  set_idt_entry(6, invalid_opcode_isr, 0, INTERRUPT_GATE, 3);
  set_idt_entry(8, double_fault_isr, 1, INTERRUPT_GATE, 3);
  set_idt_entry(10, invalid_tss_isr, 0, INTERRUPT_GATE, 3);
  set_idt_entry(13, gp_fault_isr, 0, INTERRUPT_GATE, 3);
  set_idt_entry(14, page_fault_isr, 0, INTERRUPT_GATE, 3);
  set_idt_entry(32, timer_irq_isr, 0, INTERRUPT_GATE, 0);
  set_idt_entry(33, keyboard_irq_isr, 0, INTERRUPT_GATE, 0);
  set_idt_entry(34, apic_timer_irq_isr, 0, INTERRUPT_GATE, 0);
  set_idt_entry(40, ahci_irq_isr, 0, INTERRUPT_GATE, 0);

  set_lidt(&idt_ptr);
}
