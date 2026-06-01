
.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
  lgdt (%rdi)

  mov $0x10, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %ss

  pushq $0x08
  lea .reload(%rip), %rax
  pushq %rax
  lretq

.reload:
  ret