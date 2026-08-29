#pragma once

#define HIGHER_HALF_OFFSET 0xffff800000000000ULL

#define KERNEL_HEAP_BASE 0xffff900000000000ULL
#define KERNEL_HEAP_SIZE (16 * (1UL << 30))  // 16 GiB

#define KERNEL_VMALLOC_BASE 0xffffa00000000000ULL
#define KERNEL_VMALLOC_SIZE (1 * (1UL << 40))  // 1 TiB

#define KERNEL_STACK_BASE 0xfffffe0000000000ULL
#define KERNEL_STACK_SIZE (32 * (1UL << 10))      // 32 KiB
#define KERNEL_MAX_STACK_SIZE (64 * (1UL << 30))  // 64 GiB max stack size

#define KERNEL_THREAD_STACK_SIZE (8 * (1UL << 10))  // 8 KiB

#define KERNEL_VIRTUAL_BASE 0xffffffff80000000ULL

// user space
#define USER_VIRTUAL_BASE 0x0000000000400000ULL

#define USER_STACK_BASE 0x00007FFFFFFFF000ULL  // top of the user stack
#define USER_STACK_SIZE (8 * (1UL << 20))      // 8 MiB
#define USER_KERNEL_STACK_SIZE (8 * (1UL << 10))  // 8 KiB

#define USER_MMAP_BASE 0x00007FFFF7000000ULL  // base of the user mmap area
