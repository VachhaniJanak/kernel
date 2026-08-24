#include <arch/x86_64/apic.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/timer.h>
#include <arch/x86_64/tss.h>
#include <boot/boot.h>
#include <consolefont/font.h>
#include <drivers/acpi/acpi.h>
#include <drivers/ahci/ahci.h>
#include <drivers/keyboard/ps.h>
#include <drivers/pcie/pcie.h>
#include <drivers/screen/screen.h>
#include <drivers/serial/serial.h>
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
#include <vfs/vfs.h>

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

  init_psf_font();

  mm_init();

  set_stack_top(KERNEL_STACK_BASE);

  if (!init_screen()) {
    LOG_ERROR("Framebuffer initialization failed!");
    loop();
  }

  void* addr = getRSDT();

  if (addr == NULL) {
    LOG_ERROR("Unable to get RSDT!");
    loop();
  }

  if (!initACPI(addr, &phys_to_virt)) {
    LOG_ERROR("ACPI init faild!");
    loop();
  }

  LOG_NEWLINE();
  LOG_DEBUG("CPU Count: %lu\n", getMADTEntryCount(0));

  init_timer();

  ps2_init();

  DISABLE_INT;
  init_apic();
  ENABLE_INT;

  init_pcie();

  init_ahci();

  sleep_millis(1000);

  if (!init_vfs()) {
    LOG_ERROR("VFS initialization failed!");
    loop();
  }

  kprintf("Initialization complete!\n");

  scheduler_init();

  while (true) {
    log_error("scheduler is exited");
    sleep_millis(1000);
  }
}
