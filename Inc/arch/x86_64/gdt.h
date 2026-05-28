#ifndef _ARCH_X86_64_GDT_H_
#define _ARCH_X86_64_GDT_H_

#ifndef ASM_FILE
#include <stdint.h>
#endif

#define GDT_ENTRY_SIZE 8

#define GDT_FLAG_FOUR_KILOBYTE_GRANULARITY (1 << 3)
#define GDT_FLAG_32BIT_PROTECTED_MODE (1 << 2)
#define GDT_FLAG_64BIT_MODE (1 << 1)

#define GDT_ACCESS_PRESENT (1 << 7)
#define GDT_ACCESS_PRIVILEGE_RING0 (0x0 << 5)
#define GDT_ACCESS_PRIVILEGE_RING1 (0x1 << 5)
#define GDT_ACCESS_PRIVILEGE_RING2 (0x2 << 5)
#define GDT_ACCESS_PRIVILEGE_RING3 (0x3 << 5)
#define GDT_ACCESS_EXECUTABLE (1 << 3)
#define GDT_ACCESS_DIRECTION_DOWN (1 << 2)
#define GDT_ACCESS_READABLE_WRITABLE (1 << 1)

#define DECLARE_GDT_ENTRY(base, limit, flags, access)                          \
  ((((((base)) >> 24) & 0xFF) << 56) | ((((flags)) & 0xF) << 52) |             \
   (((((limit)) >> 16) & 0xF) << 48) |                                         \
   (((((access) | (1 << 4))) & 0xFF) << 40) | ((((base)) & 0xFFF) << 16) |     \
   (((limit)) & 0xFFFF))

#define GDT_FIRST_ENTRY 0

#define GDT_KERNEL_ENTRY                                                       \
  DECLARE_GDT_ENTRY(0, 0, GDT_FLAG_64BIT_MODE,                                 \
                    GDT_ACCESS_PRESENT | GDT_ACCESS_PRIVILEGE_RING0 |          \
                        GDT_ACCESS_EXECUTABLE)

#define GDT_TABLE_ALIGNMENT 0x1000
#define GDT_TABLE_SIZE 0x800

#ifndef ASM_FILE

struct __attribute__((packed)) GDTRTypedef {
  uint16_t limit;
  unsigned long base;
};

struct __attribute__((packed)) GDTEntryTypedef {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t limit_high_flag;
  uint8_t base_high;
};
#endif
#endif // _ARCH_X86_64_GDT_H_
