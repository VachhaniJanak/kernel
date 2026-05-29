#define ASM_FILE
#include "kernel.h"
#include "sizes.h"

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/control_register.h>

#define KERNEL_GDT_ENTRY 1

#define KERNEL_CR0                                                             \
  (                                                                            \
   CONTROL_REGISTER0_PAGE |                                                    \
   CONTROL_REGISTER0_PROTECTED_MODE_ENABLED |                                  \
   CONTROL_REGISTER0_EXTENSION_TYPE                                            \
  )
#define KERNEL_CR4 (CONTROL_REGISTER4_PHYSICAL_ADDRESS_EXTENSION)

.code32
.section .boot.bss, "aw", @nobits
.align PML4_ALIGNMENT
pml4:
    .zero PML4_SIZE

.align PDPT_ALIGNMENT
low_pdpt:
    .zero PDPT_SIZE

.align PDPT_ALIGNMENT
high_pdpt:
    .zero PDPT_SIZE

.align PAGE_DIRECTORY_ALIGNMENT
low_page_directory_table:
    .zero PAGE_DIRECTORY_SIZE

.align PAGE_DIRECTORY_ALIGNMENT
high_page_directory_table:
    .zero PAGE_DIRECTORY_SIZE

.align KERNEL_BOOT_STACK_ALIGNMENT
tmp_stack:
    .zero KERNEL_BOOT_STACK_SIZE

.section .boot.data, "aw", @progbits
boot_info_base:
boot_magic:    .long 0
boot_mbi_ptr:  .long 0
pml4_ptr:      .long 0
lpdpt_ptr:     .long 0
hpdpt_ptr:     .long 0
lpdt_ptr:      .long 0
hpdt_ptr:      .long 0
gdt_info:      .long 0

.align GDT_TABLE_ALIGNMENT
.global gdt_table
gdt_table:
  .8byte GDT_FIRST_ENTRY
  .8byte GDT_KERNEL_ENTRY

gdt_table_end:
  .skip (GDT_TABLE_SIZE - (gdt_table_end - gdt_table))

gdt_ptr:
 .short GDT_TABLE_SIZE - 1
 .quad gdt_table

.section .boot.text, "ax", @progbits
.global _start
.type _start, @function
_start:
  movl $tmp_stack + KERNEL_BOOT_STACK_SIZE, %esp
  movl %eax, boot_magic                       // magic
  movl %ebx, boot_mbi_ptr                     // physical pointer
  
  movl $pml4, pml4_ptr
  movl $low_pdpt, lpdpt_ptr
  movl $high_pdpt, hpdpt_ptr
  movl $low_page_directory_table, lpdt_ptr
  movl $high_page_directory_table, hpdt_ptr
  movl $gdt_ptr, gdt_info

  movl $low_pdpt, %eax
  or $(MMU_PRESENT | MMU_WRITABLE), %eax
  movl %eax, pml4 + (PML4_ADDR_TO_ENTRY_INDEX(KERNEL_PHYSICAL_START) * PML4_ENTRY_SIZE)

  movl $high_pdpt, %eax
  or $(MMU_PRESENT | MMU_WRITABLE), %eax
  movl %eax, pml4 + (PML4_ADDR_TO_ENTRY_INDEX(KERNEL_VIRTUAL_START) * PML4_ENTRY_SIZE)

  movl $low_page_directory_table, %eax
  or $(MMU_PRESENT | MMU_WRITABLE), %eax
  movl %eax, low_pdpt + (PDPT_ADDR_TO_ENTRY_INDEX(KERNEL_PHYSICAL_START) * PDPT_ENTRY_SIZE)

  movl $high_page_directory_table, %eax
  or $(MMU_PRESENT | MMU_WRITABLE), %eax
  movl %eax, high_pdpt + (PDPT_ADDR_TO_ENTRY_INDEX(KERNEL_VIRTUAL_START) * PDPT_ENTRY_SIZE)

  mov $0, %ecx

  movl $_kernel_phy_ed , %esi
  shrl $TWO_MEGABYTES_SHIFT, %esi
  addl $1, %esi

page_directory_table_loop:
  movl $TWO_MEGABYTES, %eax
  mul %ecx
  or $(MMU_PRESENT | MMU_WRITABLE | MMU_PDE_TWO_MB), %eax
  movl %eax, low_page_directory_table(, %ecx, PAGE_DIRECTORY_ENTRY_SIZE)
  movl %eax, high_page_directory_table(, %ecx, PAGE_DIRECTORY_ENTRY_SIZE)

  inc %ecx
  cmp %esi, %ecx
  jne page_directory_table_loop  // if not equal redo loop

  movl $pml4, %eax
  movl %eax, %cr3

  movl $KERNEL_CR4, %eax
  movl %eax, %cr4

  movl $MSR_EFER, %ecx
  rdmsr
  or $MSR_EFER_LME, %eax
  wrmsr

  movl $KERNEL_CR0, %eax
  movl %eax, %cr0

  lgdt gdt_ptr

  ljmp $(KERNEL_GDT_ENTRY * GDT_ENTRY_SIZE), $_start64

  cli
  hlt

.code64
.extern kmain
.global _start64
.type _start64, @function
_start64:
  // Setup segment selectors
  movw $0, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %fs
  movw %ax, %gs
  movw %ax, %ss

  lea boot_info_base(%rip), %rdi   # rdi = &boot_info_base
  call kmain

halt:
  cli
  hlt
  jmp halt
