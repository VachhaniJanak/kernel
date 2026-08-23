#pragma once

#include <stddef.h>
#include <stdint.h>

#define PAGE_MASK (~0xfffULL)
#define PHYS_MASK (0x000FFFFFFFFFF000ULL)

#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PD_INDEX(a) (((a) >> 21) & 0x1FF)
#define PT_INDEX(a) (((a) >> 12) & 0x1FF)

#define PML4_SIZE 0x1000
#define PDPT_SIZE 0x1000
#define PD_SIZE 0x1000
#define PT_SIZE 0x1000

#define PML4_ALIGNMENT 0x1000
#define PDPT_ALIGNMENT 0x1000
#define PD_ALIGNMENT 0x1000
#define PT_ALIGNMENT 0x1000

#define PML4_ENTRY_SIZE 8
#define PDPT_ENTRY_SIZE 8
#define PD_ENTRY_SIZE 8
#define PT_ENTRY_SIZE 8

#define PML4_NUM_ENTRIES ((PML4_SIZE / PML4_ENTRY_SIZE))
#define PDPT_NUM_ENTRIES ((PDPT_SIZE / PDPT_ENTRY_SIZE))
#define PD_NUM_ENTRIES ((PD_SIZE / PD_ENTRY_SIZE))
#define PT_NUM_ENTRIES ((PT_SIZE / PT_ENTRY_SIZE))

#define MMU_PRESENT (1 << 0)
#define MMU_WRITABLE (1 << 1)
#define MMU_USER_MEMORY (1 << 2)
#define MMU_WRITE_THROUGH (1 << 3)
#define MMU_CACHE_DISABLED (1 << 4)
#define MMU_ACCESSED (1 << 5)
#define MMU_DIRTY (1 << 6)
#define MMU_HUGE_PAGE (1 << 7)
#define MMU_GLOBAL (1 << 8)
#define MMU_NO_EXECUTE (1ULL << 63)

#define MMU_PF_PLV_MASK (1 << 0)
#define MMU_PF_WA_MASK (1 << 1)
#define MMU_PF_UM_MASK (1 << 2)

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

static inline void tlb_invalidate(void* addr) {
  __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}
