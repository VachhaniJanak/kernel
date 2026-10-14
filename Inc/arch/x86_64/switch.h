#pragma once

#include <stdint.h>

static inline uint64_t read_fsbase(void) {
  uint64_t val;
  __asm__ volatile("rdfsbase %0" : "=r"(val));
  return val;
}

static inline uint64_t read_gsbase(void) {
  uint64_t val;
  __asm__ volatile("rdgsbase %0" : "=r"(val));
  return val;
}

static inline void write_fsbase(uint64_t val) {
  __asm__ volatile("wrfsbase %0" ::"r"(val));
}

static inline void write_gsbase(uint64_t val) {
  __asm__ volatile("wrgsbase %0" ::"r"(val));
}

static inline void* get_current_cpu() {
  return (void*)read_gsbase();
}

// static inline void* get_current_cpu(void) {
//   void* core;
//   __asm__ volatile("mov %%gs:0, %0" : "=r"(core));
//   return core;
// }

// switch to new thread of same process
void switch_thread(uint64_t new_stack_ptr, uint64_t new_tss_stack_ptr);

// generic context switch to new thread of different process
void switch_context(uint64_t new_page_table, uint64_t new_stack_ptr,
                    uint64_t new_tss_stack_ptr);
