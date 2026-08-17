#include <arch/x86_64/syscall.h>
#include <platform/attributes.h>
#include <stdint.h>

// MSR Addresses defined by Intel/AMD hardware manuals
#define MSR_EFER 0xC0000080            // (Extended Feature Enable Register)
#define MSR_STAR 0xC0000081            // (Long Mode Target Address Register)
#define MSR_LSTAR 0xC0000082           // (Segment Target Address Register)
#define MSR_FMASK 0xC0000084           // (Flags Mask Register)
#define MSR_GS_BASE 0xC0000101         // (GS Base Register)
#define MSR_KERNEL_GS_BASE 0xC0000102  // (Kernel GS Base Register)

extern void syscall_entry(void);

void wrmsr(uint32_t msr, uint64_t value) {
  uint32_t low = value & 0xFFFFFFFF;
  uint32_t high = value >> 32;
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

uint64_t rdmsr(uint32_t msr) {
  uint32_t low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
  return ((uint64_t)high << 32) | low;
}

void x86_64_syscall_init(void* cpu_local_data) {
  // Enable the Syscall Extension in the EFER MSR (Bit 0)
  uint64_t efer = rdmsr(MSR_EFER);
  wrmsr(MSR_EFER, efer | 1);

  // Set LSTAR to the address of our assembly entry point
  wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

  // Set the FMASK to disable interrupts (Bit 9) upon entry
  // When syscall occurs, CPU does: RFLAGS = RFLAGS & ~FMASK
  wrmsr(MSR_FMASK, 0x200);  // 0x200 is the Interrupt Flag

  // Configure the STAR register for GDT offsets
  // - Bits 32-47: Kernel Code Segment offset (0x08)
  // - Bits 48-63: Base offset for User Segments (0x10).
  //   (The CPU hardware automatically adds +8 for User Data (0x18) and +16 for
  //   User Code (0x20))
  uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48);
  wrmsr(MSR_STAR, star);

  wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu_local_data);
}

WEAK void syscall_handler(syscall_frame_t* frame) { UNUSED(frame); };
