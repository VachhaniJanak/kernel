#pragma once

#include <stdint.h>

struct regs_frame_s {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;

  uint64_t rbp;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rbx;
  uint64_t rax;
} __attribute__((packed));

struct interrupt_ecframe_s {
  struct regs_frame_s regs;

  uint64_t error_code;

  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
};

// non error code
struct interrupt_frame_s {
  struct regs_frame_s regs;

  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
};

typedef struct __attribute__((aligned(16))) {
  uint16_t fcw; // x87 FPU Control Word
  uint16_t fsw; // x87 FPU Status Word
  uint8_t ftw;  // x87 FPU Tag Word (Abridged format)
  uint8_t reserved_1;
  uint16_t fop; // x87 FPU Opcode
  uint32_t fip; // x87 FPU Instruction Pointer Offset
  uint16_t fcs; // x87 FPU Instruction Pointer Selector
  uint16_t reserved_2;
  uint32_t fdp; // x87 FPU Data Pointer Offset
  uint16_t fds; // x87 FPU Data Pointer Selector
  uint16_t reserved_3;

  uint32_t mxcsr;      // SSE Control & Status Register
  uint32_t mxcsr_mask; // Valid bits in MXCSR

  // The actual 80-bit legacy x87 registers (padded to 16 bytes each)
  uint8_t st_space[8 * 16];

  // The modern 128-bit SSE registers (XMM0 - XMM15)
  uint8_t xmm_space[16 * 16];

  uint8_t reserved_4[96];
} fpu_state_t;

struct scheduler_frame_s {
  uint64_t user_gs_base;
  uint64_t user_fs_base;

  fpu_state_t fpu_state;
  struct regs_frame_s regs;

  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
};

void page_fault_isr(void);
void invalid_tss_isr(void);
void invalid_opcode_isr(void);
void double_fault_isr(void);
void gp_fault_isr(void);
void timer_irq_isr(void);
void keyboard_irq_isr(void);
void ahci_irq_isr(void);
void apic_timer_irq_isr(void);
void simd_fpu_isr(void);

void page_fault_isr_handler(struct interrupt_ecframe_s *frame);
void invalid_tss_isr_handler(struct interrupt_ecframe_s *frame);
void invalid_opcode_isr_handler(struct interrupt_frame_s *frame);
void double_fault_isr_handler(struct interrupt_ecframe_s *frame);
void gp_fault_isr_handler(struct interrupt_ecframe_s *frame);
void timer_irq_isr_handler(struct scheduler_frame_s *frame);
void keyboard_irq_isr_handler(void);
void ahci_irq_isr_handler(void);
void apic_timer_irq_isr_handler(void);
void simd_fpu_isr_handler(struct scheduler_frame_s *frame);
