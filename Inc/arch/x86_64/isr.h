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

struct scheduler_frame_s {
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

void page_fault_isr_handler(struct interrupt_ecframe_s* frame);
void invalid_tss_isr_handler(struct interrupt_ecframe_s* frame);
void invalid_opcode_isr_handler(struct interrupt_ecframe_s* frame);
void double_fault_isr_handler(struct interrupt_ecframe_s* frame);
void gp_fault_isr_handler(struct interrupt_ecframe_s* frame);
// uint64_t timer_irq_isr_handler(struct scheduler_frame_s* frame);
void keyboard_irq_isr_handler(void);
void ahci_irq_isr_handler(void);
