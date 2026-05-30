#ifndef _KERNEL_H_
#define _KERNEL_H_


#define KERNEL_BOOT_STACK_SIZE 0x4000
#define KERNEL_BOOT_STACK_ALIGNMENT 0x1000

// START macros must have the same value in the kernel linker script
#define KERNEL_PHYSICAL_START 0x0000000000400000
#define KERNEL_VIRTUAL_START  0xFFFFFFFF80400000

#endif // _KERNEL_H_
