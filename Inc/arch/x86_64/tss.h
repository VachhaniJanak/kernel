#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096
#define EXCEPTION_STKSZ (PAGE_SIZE * 2)   /* 8KB */
#define NMI_STACK_SIZE (PAGE_SIZE * 4)    /* 16KB */
#define DOUBLEFAULT_STKSZ (PAGE_SIZE * 4) /* 16KB */
#define MCE_STACK_SIZE (PAGE_SIZE * 4)    /* 16KB */

enum cpu_entry_area_stack {
  CPU_ENTRY_AREA_STACK0 = 0, /* Normal exception stack */
  DOUBLEFAULT_STACK = 1,     /* IST1 - Double Fault */
  NMI_STACK = 2,             /* IST2 - NMI */
  DEBUG_STACK = 3,           /* IST3 - Debug (optional) */
  MCE_STACK = 4,             /* IST4 - Machine Check */
};

typedef struct __attribute__((packed)) {
  uint32_t rev0;
  uint64_t priv_stack[3];
  uint64_t rev1;
  uint64_t int_stack[7];
  uint64_t rev2;
  uint16_t rev3;
  uint16_t io_map;
} TSS_t;

typedef struct __attribute__((packed)) {
  uint16_t limit_low;       /* Limit 15:00 */
  uint16_t base_low;        /* Base 15:00 */
  uint8_t base_mid_low;     /* Base 23:16 */
  uint8_t type;             /* P(1) | DPL(2) | 0 | Type(4) */
  uint8_t limit_high_flags; /* Limit 19:16 | Flags */
  uint8_t base_mid_high;    /* Base 31:24 */
  uint32_t base_high;       /* Base 63:32 */
  uint32_t reserved;        /* Reserved (should be 0) */
} TSSDesc_t;

void tss_init(void);
