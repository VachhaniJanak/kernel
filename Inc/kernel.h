#ifndef _KERNEL_H_
#define _KERNEL_H_


#define KERNEL_BOOT_STACK_SIZE 0x4000
#define KERNEL_BOOT_STACK_ALIGNMENT 0x1000

// START macros must have the same value in the kernel linker script
#define KERNEL_PHYSICAL_START 0x0000000000400000
#define KERNEL_VIRTUAL_START  0xFFFFFFFF80400000
#ifndef ASM_FILE
#include <stdint.h>


struct boot_info_base{
    uint32_t magic;
    uint32_t mbi_ptr;
    uint32_t pml4_ptr;
    uint32_t lpdpt_ptr;
    uint32_t hpdpt_ptr;
    uint32_t lpdt_ptr;
    uint32_t hpdt_ptr;
    uint32_t gdt_info;
} __attribute__((packed));
#endif
#endif // _KERNEL_H_
