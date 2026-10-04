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
#include <mm/mm.h>
#include <mm/vmm/kheap.h>
#include <platform/attributes.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

static void loop(void) {
  while (true) {
  }
}

void kmain(void) {
  serial_init();

  LOG_NEWLINE();
  LOG_NEWLINE();
  LOG_NEWLINE();

  if (!isBootOk()) {
    LOG_ERROR("Boot failed");
    loop();
  }

  DISABLE_INT;

  gdt_init();
  tss_init();
  init_idt();

  ENABLE_INT;

  consolefont_init();

  mm_init();

  if (screen_init() != SCREEN_SUCCESS) {
    LOG_ERROR("Framebuffer initialization failed!");
    loop();
  }

  device_registry_init();

  void* addr = getRSDT();

  if (addr == NULL) {
    LOG_ERROR("Unable to get RSDT!");
    loop();
  }

  if (!initACPI(addr, &phys_to_virt)) {
    LOG_ERROR("ACPI init faild!");
    loop();
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

  while (true) {
    log_error("scheduler is exited");
    timer_sleep_ms(5000);
  }
}
