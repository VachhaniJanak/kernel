#pragma once

#include <stdint.h>

typedef struct __attribute__((packed)) {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t arg4;  // r10
  uint64_t arg6;  // r9
  uint64_t arg5;  // r8

  uint64_t rbp;
  uint64_t arg1;  // rdi
  uint64_t arg2;  // rsi
  uint64_t arg3;  // rdx
  uint64_t rbx;
  uint64_t syscall_num;  // rax

  uint64_t r11;  // The User's saved RFLAGS
  uint64_t rcx;  // The User's saved RIP (Instruction Pointer)
} syscall_frame_t;

void x86_64_syscall_init(void* cpu_local_data);
