#define PUSH_X86_64_REGS \
  push %rax;            \
  push %rbx;            \
  push %rdx;            \
  push %rsi;            \
  push %rdi;            \
  push %rbp;            \
  push %r8;             \
  push %r9;             \
  push %r10;            \
  push %r12;            \
  push %r13;            \
  push %r14;            \
  push %r15

#define POP_X86_64_REGS \
  pop %r15;            \
  pop %r14;            \
  pop %r13;            \
  pop %r12;            \
  pop %r10;            \
  pop %r9;             \
  pop %r8;             \
  pop %rbp;            \
  pop %rdi;            \
  pop %rsi;            \
  pop %rdx;            \
  pop %rbx;            \
  pop %rax


.extern syscall_handler

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
  // Swap the GS register to access our hidden kernel pointers
  swapgs

  // Save the user's stack pointer into a safe memory location via GS
  mov %rsp, %gs:0x00

  // Load the secure Kernel Stack pointer into RSP
  mov %gs:0x08, %rsp

  push %rcx // User RIP (Instruction Pointer)
  push %r11 // User RFLAGS

  PUSH_X86_64_REGS
  mov %rsp, %rdi
  call syscall_handler
  POP_X86_64_REGS

  pop %r11 // User RFLAGS
  pop %rcx // User RIP

  // Restore the user's stack pointer from the safe memory location via GS
  mov %gs:0x00, %rsp

  // Swap the GS register back to the user GS
  swapgs

  // Return to the user code
  sysretq

