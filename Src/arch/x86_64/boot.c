#include <arch/x86_64/gdt.h>
#include <arch/x86_64/mmu.h>
#include <platform/attributes.h>
#include <stddef.h>
#include <stdint.h>

extern size_t __phy_addr_st;
extern size_t __virt_addr_st;

void kmain(void);

BOOT_BSS_SECTION static uintptr_t PML4[PML4_SIZE]
    __attribute__((aligned(PML4_ALIGNMENT))) = {0};

BOOT_BSS_SECTION static uintptr_t LOW_PDPT[PDPT_SIZE]
    __attribute__((aligned(PDPT_ALIGNMENT))) = {0};

BOOT_BSS_SECTION static uintptr_t HIGH_PDPT[PDPT_SIZE]
    __attribute__((aligned(PDPT_ALIGNMENT))) = {0};

BOOT_BSS_SECTION static uintptr_t PageDirectory[PAGE_DIRECTORY_SIZE]
    __attribute__((aligned(PAGE_DIRECTORY_ALIGNMENT))) = {0};


BOOT_TEXT_SECTION
static inline void map_virtual_kernel(void) {
  uintptr_t virt_addr = (uintptr_t)&__virt_addr_st;
  uintptr_t phy_addr = (uintptr_t)&__phy_addr_st;

  size_t pml4_index = PML4_ADDR_TO_ENTRY_INDEX(phy_addr);
  PML4[pml4_index] = (uintptr_t)LOW_PDPT | MMU_PRESENT | MMU_WRITABLE;

  pml4_index = PML4_ADDR_TO_ENTRY_INDEX(virt_addr);
  PML4[pml4_index] = (uintptr_t)HIGH_PDPT | MMU_PRESENT | MMU_WRITABLE;

  size_t pdpt_index = PDPT_ADDR_TO_ENTRY_INDEX(phy_addr);
  LOW_PDPT[pdpt_index] = (uintptr_t)PageDirectory | MMU_PRESENT | MMU_WRITABLE;

  pdpt_index = PDPT_ADDR_TO_ENTRY_INDEX(virt_addr);
  HIGH_PDPT[pdpt_index] = (uintptr_t)PageDirectory | MMU_PRESENT | MMU_WRITABLE;

  size_t pd_entries = PAGE_DIRECTORY_SIZE / PAGE_DIRECTORY_ENTRY_SIZE;
  size_t mmu_flags = MMU_PRESENT | MMU_WRITABLE | MMU_PDE_TWO_MB;

  for (size_t i = 0; i < pd_entries; i++)
    PageDirectory[i] = (i * 0x200000) | mmu_flags;

  uintptr_t pml4_phys_addr = (uintptr_t)PML4;
  __asm__ ("mov %0, %%cr3" : : "r"(pml4_phys_addr) : "memory");
}

BOOT_TEXT_SECTION
void _start(void) {

  /*
   * Multiboot:
   * RAX = magic
   * RBX = multiboot info
   */
  __asm__ volatile("mov %rax, %rdi");
  __asm__ volatile("mov %rbx, %rsi");

  // Clear segments
  __asm__ volatile("xor %ax, %ax");
  __asm__ volatile("mov %ax, %ds");
  __asm__ volatile("mov %ax, %es");
  __asm__ volatile("mov %ax, %fs");
  __asm__ volatile("mov %ax, %gs");
  __asm__ volatile("mov %ax, %ss");


  map_virtual_kernel();
  kmain();

  for (;;)
    __asm__ volatile("hlt");
}
