#define PUSH_X86_64_REGS  \
  push %rax;              \
  push %rbx;              \
  push %rcx;              \
  push %rdx;              \
  push %rsi;              \
  push %rdi;              \
  push %rbp;              \
  push %r8;               \
  push %r9;               \
  push %r10;              \
  push %r11;              \
  push %r12;              \
  push %r13;              \
  push %r14;              \
  push %r15

#define POP_X86_64_REGS   \
  pop %r15;               \
  pop %r14;               \
  pop %r13;               \
  pop %r12;               \
  pop %r11;               \
  pop %r10;               \
  pop %r9;                \
  pop %r8;                \
  pop %rbp;               \
  pop %rdi;               \
  pop %rsi;               \
  pop %rdx;               \
  pop %rcx;               \
  pop %rbx;               \
  pop %rax

  
.extern lapic_eoi

// Define an ISR that does not push an error code
#define DEFINE_ISR_NE_X86_64(name)                              \
  .global name;                                                 \
  .extern name##_handler;                                       \
  .type name, @function;                                        \
  name:                                                         \
    /*                                                            
      check if the interrupt is coming from Ring 0 or Ring 3?   
      If the lowest 2 bits of CS are 0,                         
      then we are returning to Ring 0.                          
      Otherwise, we are returning to Ring 3.                      
    */                                                          \
    testq $3, 8(%rsp);                                          \
    jz .skip_swapgs##name;                                      \
    swapgs;                                                     \
    .skip_swapgs##name:                                         \
      PUSH_X86_64_REGS;                                         \
      mov %rsp, %rdi;                                           \
      call name##_handler;                                      \
      POP_X86_64_REGS;                                          \
      testq $3, 8(%rsp);                                        \
      jz .return_to_kernel##name;                               \
      swapgs;                                                   \
      .return_to_kernel##name:                                  \
        iretq

// Define an ISR that pushes an error code
#define DEFINE_ISR_X86_64(name)                                 \
  .global name;                                                 \
  .extern name##_handler;                                       \
  .type name, @function;                                        \
  name:                                                         \
    /*                                                            
      check if the interrupt is coming from Ring 0 or Ring 3?       
      If the lowest 2 bits of CS are 0,                           
      then we are returning to Ring 0.                              
      Otherwise, we are returning to Ring 3.                      
    */                                                          \
    testq $3, 16(%rsp);                                         \
    jz .skip_swapgs##name;                                      \
    swapgs;                                                     \
    .skip_swapgs##name:                                         \
      PUSH_X86_64_REGS;                                         \
      mov %rsp, %rdi;                                           \
      call name##_handler;                                      \
      POP_X86_64_REGS;                                          \
      add $8, %rsp;                                             \
      testq $3, 8(%rsp);                                        \
      jz .return_to_kernel##name;                               \
      swapgs;                                                   \
      .return_to_kernel##name:                                  \
        iretq

// Define an ISR that does not push an error code
// and call lapic_eoi
#define DEFINE_ISR_NE_EOI_X86_64(name)                          \
  .global name;                                                 \
  .extern name##_handler;                                       \
  .type name, @function;                                        \
  name:                                                         \
    /*                                                            
      check if the interrupt is coming from Ring 0 or Ring 3?     
      If the lowest 2 bits of CS are 0,                               
      then we are returning to Ring 0.                                  
      Otherwise, we are returning to Ring 3.                        
    */                                                          \
    testq $3, 8(%rsp);                                          \
    jz .skip_swapgs##name;                                      \
    swapgs;                                                     \
    .skip_swapgs##name:                                         \
      PUSH_X86_64_REGS;                                         \
      mov %rsp, %rdi;                                           \
      call name##_handler;                                      \
      call lapic_eoi;                                           \
      POP_X86_64_REGS;                                          \
      testq $3, 8(%rsp);                                        \
      jz .return_to_kernel##name;                               \
      swapgs;                                                   \
      .return_to_kernel##name:                                  \
        iretq  

.section .text
DEFINE_ISR_X86_64(page_fault_isr);

DEFINE_ISR_X86_64(gp_fault_isr);

DEFINE_ISR_X86_64(invalid_tss_isr);

DEFINE_ISR_X86_64(invalid_opcode_isr);

DEFINE_ISR_X86_64(double_fault_isr);

DEFINE_ISR_NE_EOI_X86_64(keyboard_irq_isr);

DEFINE_ISR_NE_EOI_X86_64(ahci_irq_isr);

DEFINE_ISR_NE_EOI_X86_64(apic_timer_irq_isr);


.global timer_irq_isr
.extern timer_irq_isr_handler
.extern check_pending_signals
.type timer_irq_isr, @function
timer_irq_isr:
  testq $3, 8(%rsp)
  jz .skip_swapgs_timer_irq_isr
  swapgs
  .skip_swapgs_timer_irq_isr:
    PUSH_X86_64_REGS

    rdfsbase %rax     // Read FS base address into register RAX
    swapgs            // swap to user GS base
    rdgsbase %rbx     // Read GS base address into register RBX
    swapgs            // swap back to kernel GS base
    push %rax         // Save FS base on the stack
    push %rbx         // Save GS base on the stack

    mov %rsp, %rdi 

    call timer_irq_isr_handler
    call lapic_eoi

    pop %rbx          // Restore GS base from the stack
    pop %rax          // Restore FS base from the stack
    wrfsbase %rax     // Write the address in RAX to the FS base
    swapgs            // swap to user GS base
    wrgsbase %rbx     // Write the address in RBX to the GS base
    swapgs            // swap back to kernel GS base

    testq $3, 128(%rsp)
    jz .return_to_kernel_timer_irq_isr
    
    // Check for pending signals before returning to user space 
    call check_pending_signals

    swapgs
    .return_to_kernel_timer_irq_isr:
      POP_X86_64_REGS
      iretq  
