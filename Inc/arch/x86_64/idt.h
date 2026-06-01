#pragma once
#include <stdint.h>

#define INTERRUPT_GATE 0XE
#define TRAP_GATE 0XF

typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint64_t base;
} IDTRTypedef;

typedef struct __attribute__((packed)) {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist_zero;   // 0-2 bits for ist, 3-7 bits reserved
  uint8_t attributes; // 0 bit for gate type, 1-3 bits must be one, 4 bits must
                      // be zero, 5-6 dpl, 7 peresnt
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t reserved;
} IDTEntryTypedef;

void init_idt(void);
