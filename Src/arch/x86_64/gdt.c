#include <arch/x86_64/gdt.h>
#include <stdint.h>

#include "helper.h"

static GDTEntry_t gdt_table[256]
    __attribute__((aligned(GDT_TABLE_ALIGNMENT))) = {0};

void* get_gdt_base(void) { return (void*)gdt_table; }

void set_gdt_entry(int index, uint32_t base, uint32_t limit, uint8_t flags,
                   uint8_t access) {
  gdt_table[index].limit_low = limit & 0xFFFF;
  gdt_table[index].base_low = base & 0xFFFF;
  gdt_table[index].base_mid = (base >> 16) & 0xFF;
  gdt_table[index].access = access;
  gdt_table[index].limit_high_flag =
      ((limit >> 16) & 0x0F) | ((flags & 0x0F) << 4);
  gdt_table[index].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void) {
  // Null descriptor
  set_gdt_entry(GDT_NULL, 0, 0, 0, 0);

  // Kernel code segment
  set_gdt_entry(GDT_KERNEL_CODE, 0, 0, GDT_FLAG_64BIT_MODE,
                GDT_ACCESS_PRESENT | GDT_ACCESS_DESCRIPTOR_TYPE |
                    GDT_ACCESS_PRIVILEGE_RING0 | GDT_ACCESS_EXECUTABLE |
                    GDT_ACCESS_READABLE_WRITABLE);

  // Kernel data segment
  set_gdt_entry(GDT_KERNEL_DATA, 0, 0, 0,
                GDT_ACCESS_PRESENT | GDT_ACCESS_DESCRIPTOR_TYPE |
                    GDT_ACCESS_PRIVILEGE_RING0 | GDT_ACCESS_READABLE_WRITABLE);

  GDTR_t gdt_ptr = {
      .limit = sizeof(gdt_table) - 1,
      .base = (uint64_t)gdt_table,
  };

  // Load the GDT
  gdt_flush(&gdt_ptr);
}
