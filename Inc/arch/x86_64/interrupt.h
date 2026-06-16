#pragma once

#include <stdint.h>

#define DISABLE_INT __asm__ volatile("cli");
#define ENABLE_INT __asm__ volatile("sti");

typedef struct __attribute__((packed)) {
  // 1. Manually saved general-purpose registers (pushed by Assembly stub)
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

  // 2. Automatically pushed by CPU for specific exceptions
  uint64_t error_code;

  // 3. Automatically pushed by CPU for all interrupts
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} interrupt_frame_with_err_t;

typedef struct __attribute__((packed)) {
  // 1. Manually saved general-purpose registers
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

  // 2. Automatically pushed by CPU for all interrupts
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} interrupt_frame_no_err_t;
