#include <arch/x86_64/apic.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/timer.h>
#include <arch/x86_64/tss.h>
#include <boot/boot.h>
#include <consolefont/font.h>
#include <device/device.h>
#include <drivers/acpi/acpi.h>
#include <drivers/ahci/ahci.h>
#include <drivers/pcie/pcie.h>
#include <drivers/screen/screen.h>
#include <drivers/serial/serial.h>
#include <fs/fs.h>
#include <input/input.h>
#include <kernel.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <platform/attributes.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/panic.h>
#include <utils/utils.h>

void kmain(void) {
  serial_init();

  LOG_NEWLINE();
  LOG_NEWLINE();
  LOG_NEWLINE();

  if (!isBootOk()) {
    kernel_panic("Boot failed!");
  }

  DISABLE_INT;

  gdt_init();
  tss_init();
  init_idt();

  ENABLE_INT;

  consolefont_init();

  mm_init();

  if (screen_init() != SCREEN_SUCCESS) {
    kernel_panic("Screen initialization failed!");
  }

  device_registry_init();

  void* addr = getRSDT();

  if (!addr) {
    kernel_panic("Unable to get RSDT!");
  }

  if (!initACPI(addr, &phys_to_virt)) {
    kernel_panic("ACPI initialization failed!");
  }

  timer_init();

  input_init();

  DISABLE_INT;
  init_apic();
  ENABLE_INT;

  init_pcie();

  ahci_init();

  fs_init();

  scheduler_init();

  kernel_panic("Kernel panic: Reached end of kmain()!");
}
