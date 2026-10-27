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

.extern lapic_eoi
.extern set_tss_ring_x_stack
.extern check_pending_signals

isr_exit:
  // if lowest 2 bits of CS are 0, then we are returning to Ring 0  
  testq $3, 128(%rsp)
  jz return_to_kernel
  
  // Check for pending signals before returning to user space 
  call check_pending_signals

  // returning to Ring 3
  // Restore the user's GS pointer.
  swapgs
    
return_to_kernel:
  POP_X86_64_REGS
  iretq

.global switch_context
.type switch_context, @function
switch_context:
  // arg1: Switch to the new page table
  mov %rdi, %cr3

  // arg2: Switch to the new stack pointer 
  mov %rsi, %rsp

  // Set the TSS stack pointer for ring 0
  mov %rdx, %rdi
  mov $0, %rsi

  call set_tss_ring_x_stack
  call lapic_eoi

  pop %rbx          // Restore GS base from the stack
  pop %rax          // Restore FS base from the stack
  wrfsbase %rax     // Write the address in RAX to the FS base
  swapgs            // swap to user GS base
  wrgsbase %rbx     // Write the address in RBX to the GS base
  swapgs            // swap back to kernel GS base
  
  jmp isr_exit

.global switch_thread
.type switch_thread, @function
switch_thread:
  mov %rdi, %rsp

  // Set the TSS stack pointer for ring 0
  mov %rsi, %rdi
  mov $0, %rsi
  
  call set_tss_ring_x_stack
  call lapic_eoi

  pop %rbx          // Restore GS base from the stack
  pop %rax          // Restore FS base from the stack
  wrfsbase %rax     // Write the address in RAX to the FS base
  swapgs            // swap to user GS base
  wrgsbase %rbx     // Write the address in RBX to the GS base
  swapgs            // swap back to kernel GS base
  
  jmp isr_exit
