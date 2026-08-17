#pragma once

#include <stddef.h>
#include <stdint.h>

#define PAGE_MASK (~0xfffULL)

#define PML4_SIZE 0x1000
#define PML4_ALIGNMENT 0x1000
#define PML4_ENTRY_SIZE 8
#define PML4_ADDR_TO_ENTRY_INDEX(addr) (((addr) >> 39) & 0x1FF)

#define PDPT_SIZE 0x1000
#define PDPT_ALIGNMENT 0x1000
#define PDPT_ENTRY_SIZE 8
#define PDPT_ADDR_TO_ENTRY_INDEX(addr) (((addr) >> 30) & 0x1FF)

#define PAGE_DIRECTORY_SIZE 0x1000
#define PAGE_DIRECTORY_ALIGNMENT 0x1000
#define PAGE_DIRECTORY_ENTRY_SIZE 8
#define PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX(addr) (((addr) >> 21) & 0x1FF)

#define PAGE_TABLE_SIZE 0x1000
#define PAGE_TABLE_ALIGNMENT 0x1000
#define PAGE_TABLE_ENTRY_SIZE 8
#define PAGE_TABLE_ADDR_TO_ENTRY_INDEX(addr) (((addr) >> 12) & 0x1FF)

#define MMU_PRESENT (1 << 0)
#define MMU_WRITABLE (1 << 1)
#define MMU_USER_MEMORY (1 << 2)
#define MMU_PDE_TWO_MB (1 << 7)
#define MMU_NX (1ULL << 63)

static inline uintptr_t get_page_table_addr(void) {
  uintptr_t page_table_addr;
  __asm__ volatile("mov %%cr3, %0" : "=r"(page_table_addr) : : "memory");
  return page_table_addr;
}

static inline void set_page_table_addr(uintptr_t addr) {
  __asm__ volatile("mov %0, %%cr3" : : "r"(addr) : "memory");
}

static inline size_t page_fault_addr(void) {
  size_t val;
  __asm__ volatile("mov %%cr2, %0" : "=r"(val));
  return val;
}

static inline void tlb_invalided(void* addr) {
  __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}
