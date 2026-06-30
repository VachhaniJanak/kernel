#define PUSH_X86_64_REGS \
  push %rax;            \
  push %rbx;            \
  push %rcx;            \
  push %rdx;            \
  push %rsi;            \
  push %rdi;            \
  push %rbp;            \
  push %r8;             \
  push %r9;             \
  push %r10;            \
  push %r11;            \
  push %r12;            \
  push %r13;            \
  push %r14;            \
  push %r15

#define POP_X86_64_REGS \
  pop %r15;            \
  pop %r14;            \
  pop %r13;            \
  pop %r12;            \
  pop %r11;            \
  pop %r10;            \
  pop %r9;             \
  pop %r8;             \
  pop %rbp;            \
  pop %rdi;            \
  pop %rsi;            \
  pop %rdx;            \
  pop %rcx;            \
  pop %rbx;            \
  pop %rax

// Define an ISR that does not push an error code
#define DEFINE_ISR_NE_X86_64(name) \
  .global name;                    \
  .extern name##_handler;          \
  .type name, @function;           \
  name:                            \
  PUSH_X86_64_REGS;                \
  mov %rsp, %rdi;                  \
  call name##_handler;             \
  POP_X86_64_REGS;                 \
  iretq


// Define an ISR that pushes an error code
#define DEFINE_ISR_X86_64(name) \
  .global name;                 \
  .extern name##_handler;       \
  .type name, @function;        \
  name:                         \
  PUSH_X86_64_REGS;             \
  mov %rsp, %rdi;               \
  call name##_handler;          \
  POP_X86_64_REGS;              \
  add $8, %rsp;                 \
  iretq


.section .text
DEFINE_ISR_X86_64(page_fault_isr);

DEFINE_ISR_X86_64(gp_fault_isr);

DEFINE_ISR_X86_64(invalid_tss_isr);

DEFINE_ISR_X86_64(invalid_opcode_isr);

DEFINE_ISR_X86_64(double_fault_isr);

DEFINE_ISR_X86_64(timer_irq_isr);

DEFINE_ISR_NE_X86_64(keyboard_irq_isr);