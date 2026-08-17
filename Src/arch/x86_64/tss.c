#include <arch/x86_64/gdt.h>
#include <arch/x86_64/tss.h>
#include <stdint.h>

static TSS_t tss __attribute__((aligned(16)));

void set_tss_desc(uint64_t base, uint32_t limit) {
  // TSS descriptor is the 5th entry in GDT (index 5, selector 0x28)
  uint8_t* gdt_base = (uint8_t*)get_gdt_base();
  TSSDesc_t* td = (TSSDesc_t*)(gdt_base + 8 * GDT_TSS);

  // set limit
  td->limit_low = limit & 0xffff;

  // set flags: G(0) | D(1) | 0 | AVL(0) | Limit(19:16)
  td->limit_high_flags = (limit >> 16) & 0xf;

  // set base
  td->base_low = base & 0xffff;
  td->base_mid_low = (base >> 16) & 0xff;
  td->base_mid_high = (base >> 24) & 0xff;
  td->base_high = (base >> 32) & 0xffffffff;

  // set type field: P(1) | DPL (00) | S(0) | TYPE(1001)
  td->type = 0x89;

  td->reserved = 0;
}

static inline void set_ltr(uint16_t selector) {
  __asm__ volatile("ltr %0" : : "r"(selector));
}

void tss_init(void) {
  // Set up the TSS descriptor in the GDT
  set_tss_desc((uint64_t)&tss, sizeof(TSS_t) - 1);

  // Load the TSS
  uint16_t tss_selector = GDT_TSS << 3;  // Selector is index shifted by 3
  set_ltr(tss_selector);
}

void set_tss_ring_x_stack(void* ptr, uint8_t ring) {
  tss.priv_stack[ring] = (uint64_t)ptr;
}
