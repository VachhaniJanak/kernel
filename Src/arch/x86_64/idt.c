#include <arch/x86_64/idt.h>
#include <stddef.h>
#include <stdint.h>

extern void PageFaultHandler(void);
extern void InvalidTSSHandler(void);
extern void InvalidOpcodeHandler(void);
extern void DoubleFaultHandler(void);
extern void GeneralProtectionFaultHandler(void);
extern void TimerIRQHandler(void);

IDTEntryTypedef idt[256] __attribute__((aligned(16))) = {0};

void set_idt_entry(size_t vector, void (*handler)(void), uint8_t ist,
                   uint8_t g_type, uint8_t dpl) {

  uintptr_t addr = (uintptr_t)handler;

  idt[vector].selector = 0x08; // 0-1 RPL=0, 2 TI=0, 3-15 Index=1
  idt[vector].offset_low = 0xffff & addr;
  idt[vector].offset_mid = 0xffff & (addr >> 16);
  idt[vector].offset_high = 0xffffffff & (addr >> 32);
  idt[vector].reserved = 0;
  idt[vector].ist_zero = 0x7 & ist;
  idt[vector].attributes = 0;
  idt[vector].attributes = (0xf & g_type) | ((0x3 & dpl) << 5) | (1 << 7);
}

static inline void set_lidt(void *ptr) {
  __asm__ volatile("lidt (%0)" : : "r"(ptr) : "memory");
}

void init_idt(void) {

  set_idt_entry(14, PageFaultHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(10, InvalidTSSHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(6, InvalidOpcodeHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(8, DoubleFaultHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(13, GeneralProtectionFaultHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(32, TimerIRQHandler, 0, INTERRUPT_GATE, 0);

  IDTRTypedef idt_ptr = {.limit = sizeof(idt) - 1, .base = (uint64_t)idt};

  set_lidt(&idt_ptr);
}
