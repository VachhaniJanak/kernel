#ifndef _ARCH_X86_64_GDT_H_
#define _ARCH_X86_64_GDT_H_

#define GDT_ENTRY_SIZE 8
#define GDT_FIRST_ENTRY 0
#define GDT_TABLE_ALIGNMENT 0x1000
#define GDT_TABLE_SIZE 0x800

#define GDT_FLAG_FOUR_KILOBYTE_GRANULARITY (1 << 3)
#define GDT_FLAG_32BIT_PROTECTED_MODE (1 << 2)
#define GDT_FLAG_64BIT_MODE (1 << 1)

#define GDT_ACCESS_DESCRIPTOR_TYPE (1 << 4)

#define GDT_ACCESS_PRESENT (1 << 7)
#define GDT_ACCESS_PRIVILEGE_RING0 (0x0 << 5)
#define GDT_ACCESS_PRIVILEGE_RING1 (0x1 << 5)
#define GDT_ACCESS_PRIVILEGE_RING2 (0x2 << 5)
#define GDT_ACCESS_PRIVILEGE_RING3 (0x3 << 5)
#define GDT_ACCESS_EXECUTABLE (1 << 3)
#define GDT_ACCESS_DIRECTION_DOWN (1 << 2)
#define GDT_ACCESS_READABLE_WRITABLE (1 << 1)

enum {
  GDT_NULL = 0,
  GDT_KERNEL_CODE = 1,
  GDT_KERNEL_DATA = 2,
  GDT_USER_CODE = 3,
  GDT_USER_DATA = 4,
  GDT_TSS = 5,
};

struct __attribute__((packed)) GDTR_s {
  uint16_t limit;
  uint64_t base;
};

struct __attribute__((packed)) GDTEntry_s {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t limit_high_flag;
  uint8_t base_high;
};

typedef struct GDTR_s GDTR_t;
typedef struct GDTEntry_s GDTEntry_t;

void set_gdt_entry(int index, uint32_t base, uint32_t limit, uint8_t flags,
                   uint8_t access);

void gdt_init(void);

void *get_gdt_base(void);

#endif // _ARCH_X86_64_GDT_H_
