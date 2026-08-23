#pragma once

#include <platform/attributes.h>
#include <stdint.h>

FORCE_INLINE inline void set_stack_top(uintptr_t stack_top) {
  __asm__ volatile("mov %0, %%rsp" : : "r"(stack_top) : "memory");
}

static inline uintptr_t get_stack_top(void) {
  uintptr_t stack_top;
  __asm__ volatile("mov %%rsp, %0" : "=r"(stack_top));
  return stack_top;
}