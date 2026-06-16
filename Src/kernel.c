#include <boot/boot.h>
#include <kernel.h>

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/tss.h>

#include <drivers/screen/screen.h>
#include <drivers/serial/serial.h>

#include <mm/mm.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <utils/log.h>
#include <utils/utils.h>

// Halt and catch fire function.
static void hcf(void) {
  for (;;)
    asm("hlt");
}

void printMemoryMap(void) {

  struct MemoryMapEntry_s entries[getMMapEntryCount()];

  if (!copyMMapEntry(entries)) {
    serial_printf("Failed to copy memory map entries.\n");
    return;
  }

  for (size_t i = 0; i < getMMapEntryCount(); i++) {
    uint64_t base = entries[i].base;
    uint64_t length = entries[i].length;
    uint64_t type = entries[i].type;

    switch (type) {
    case LIMINE_MEMMAP_USABLE:
      serial_printf(MARK_AS_BOLD("Usable                 : "));
      break;
    case LIMINE_MEMMAP_RESERVED:
      serial_printf(MARK_AS_BOLD("Reserved               : "));
      break;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
      serial_printf(MARK_AS_BOLD("ACPI reclaimable       : "));
      break;
    case LIMINE_MEMMAP_ACPI_NVS:
      serial_printf(MARK_AS_BOLD("ACPI NVS               : "));
      break;
    case LIMINE_MEMMAP_BAD_MEMORY:
      serial_printf(MARK_AS_BOLD("Bad                    : "));
      break;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
      serial_printf(MARK_AS_BOLD("Bootloader reclaimable : "));
      break;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
      serial_printf(MARK_AS_BOLD("Executable and modules : "));
      break;
    case LIMINE_MEMMAP_FRAMEBUFFER:
      serial_printf(MARK_AS_BOLD("Framebuffer            : "));
      break;
    case LIMINE_MEMMAP_RESERVED_MAPPED:
      serial_printf(MARK_AS_BOLD("Reserved mapped        : "));
      break;
    default:
      serial_printf(MARK_AS_BOLD("Unknown  type          : "));
      break;
    }

    serial_printf("Base: 0x%016lx, Length: %lu MiB\r\n", base,
                  length / (1024 * 1024));
  }
}

void kmain(void) {
  serial_init();

  LOG_NEWLINE();
  LOG_NEWLINE();
  LOG_NEWLINE();

  if (!isBootOk()) {
    LOG_ERROR("Boot failed");
    hcf();
  }

  DISABLE_INT;

  gdt_init();
  tss_init();
  init_idt();

  ENABLE_INT;

  mm_init();

  // printMemoryMap();

  if (!init_screen()) {
    LOG_ERROR("Framebuffer initialization failed!");
    hcf();
  }

  clear_screen(0x00000000);
  kprintf("Welcome to %s!\n", "MyOS");

  while (1)
    ;
}
