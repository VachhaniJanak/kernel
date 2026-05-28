#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <stdint.h>

extern void PageFaultHandler(void);
extern void InvalidTSSHandler(void);
extern void InvalidOpcodeHandler(void);
extern void DoubleFaultHandler(void);
extern void GeneralProtectionFaultHandler(void);
// extern uint64_t gdt_table;

IDTEntryTypedef idt[256] __attribute__((aligned(16))) = {0};
TSSTypedef tss __attribute__((aligned(16)));
uint8_t page_fault_stack[16384] = {0};

void set_idt_entry(int vector, void *handler, uint8_t ist, uint8_t g_type,
                   uint8_t dpl) {

  uint64_t addr = (uint64_t)handler;

  idt[vector].selector = 0x08; // 0-1 RPL=0, 2 TI=0, 3-15 Index=1
  idt[vector].offset_low = 0xffff & addr;
  idt[vector].offset_mid = 0xffff & (addr >> 16);
  idt[vector].offset_high = 0xffffffff & (addr >> 32);
  idt[vector].reserved = 0;
  idt[vector].ist_zero = 0x7 & ist;
  idt[vector].attributes = 0;
  idt[vector].attributes = (0xf & g_type) | ((0x3 & dpl) << 5) | (1 << 7);
}

// void set_tss_desc(uint64_t base, uint32_t limit) {

//   // TSS descriptor is the 3rd entry in GDT, so we add 16 bytes to the base of GDT
//   TSSDescTypedef *td = (TSSDescTypedef *)((uint8_t *)&gdt_table + 8 * 2);

//   // set limit
//   td->limit_low = limit & 0xffff;
//   td->limit_high_flags = (limit >> 16) & 0xf;

//   // set base
//   td->base_low = base & 0xffff;
//   td->base_mid_low = (base >> 16) & 0xff;
//   td->base_mid_high = (base >> 24) & 0xff;
//   td->base_high = (base >> 32) & 0xffffffff;

//   // set type field: P(1) | DPL (00) | S(0) | TYPE(1001)
//   td->type = 0x89;

//   // set flags: G(0) | D(1) | 0 | AVL(0) | Limit(19:16)

//   td->reserved = 0;
// }

void init_idt(void) {

  // tss.int_stack[0] = (uint64_t)page_fault_stack + 16384;
  // set_tss_desc((uint64_t)&tss, sizeof(TSSTypedef) - 1);

  set_idt_entry(14, PageFaultHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(10, InvalidTSSHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(6, InvalidOpcodeHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(8, DoubleFaultHandler, 0, INTERRUPT_GATE, 3);
  set_idt_entry(13, GeneralProtectionFaultHandler, 0, INTERRUPT_GATE, 3);

  // right shift index(from 0) of TSS descriptor in GDT by 3 to get the selector 
  // uint16_t tss_selector = 2 << 3;
  // __asm__ volatile("ltr %w0" ::"r"(tss_selector));

  IDTRTypedef idt_ptr = {.limit = sizeof(idt) - 1, .base = (uint64_t)idt};
  __asm__ volatile("lidt %0" ::"m"(idt_ptr));
}
