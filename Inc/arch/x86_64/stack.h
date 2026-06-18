#pragma once

#include <stdint.h>
#include <platform/attributes.h>

FORCE_INLINE inline void set_stack_top(uintptr_t stack_top) {
  __asm__ volatile("mov %0, %%rsp" : : "r"(stack_top) : "memory");
}
