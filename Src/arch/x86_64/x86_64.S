.section .text
.global fpu_hardware_init
.type fpu_hardware_init, @function
fpu_hardware_init:
  // Configure CR0 (The legacy x87 FPU extensions)
  mov %cr0, %rax
  btr $3, %rax       // Clear TS (Bit 3) - Disable Lazy FPU switching
  btr $2, %rax       // Clear EM (Bit 2) - Disable software emulation
  bts $1, %rax       // Set MP (Bit 1) - Enable FPU monitoring
  mov %rax, %cr0 

  // Configure CR4 (The modern SSE/XMM extensions)
  mov %cr4, %rax 
  bts $9, %rax   // Set OSFXSR (Bit 9) - Enable fxsave/fxrstor 
  bts $10, %rax  // Set OSXMMEXCPT (Bit 10) - OS will handle SIMD
  mov %rax, %cr4

  fninit
  ret