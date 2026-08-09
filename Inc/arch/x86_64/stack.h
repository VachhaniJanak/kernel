#pragma once

#include <platform/attributes.h>
#include <stdint.h>

FORCE_INLINE inline void set_stack_top(uintptr_t stack_top) {
  __asm__ volatile("mov %0, %%rsp" : : "r"(stack_top) : "memory");
}

inline uint64_t get_stack_top(void) {
  uint64_t stack_top;
  __asm__ volatile("mov %%rsp, %0" : "=r"(stack_top));
  return stack_top;
}