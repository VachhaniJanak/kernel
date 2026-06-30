#include <arch/x86_64/io.h>
#include <drivers/acpi/acpi.h>
#include <mm/mm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

#define MASTER_PIC_CMD 0x20
#define MASTER_PIC_DATA 0x21
#define SLAVE_PIC_CMD 0xA0
#define SLAVE_PIC_DATA 0xA1

// Offset
#define APIC_ID 0x20
#define APIC_VERSION 0x30
#define TASK_PRIORITY 0x80
#define EOI 0xB0
#define SIV 0xF0
#define ICR_LOW 0x300
#define ICR_HIGH 0x310
#define TIMER 0x320
#define INITIAL_COUNT 0x380
#define CURRENT_COUNT 0x390

// LVT
#define LVT_TIMER 0x320
#define LVT_THERMAL 0x330
#define LVT_PERF 0x340
#define LVT_LINT0 0x350
#define LVT_LINT1 0x360
#define LVT_ERROR 0x370

#define ENABLE_APIC 0x00000100
#define SVECTOR_MASK 0xFF

#define IOREGSEL 0x00
#define IOWIN 0x10

#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOREDTBL 0x10

static uint32_t* lapic_base = NULL;
static uint32_t* ioapic_base = NULL;

static inline void lapic_write(uint32_t reg, uint32_t value) {
  lapic_base[reg / 4] = value;
}

static inline uint32_t lapic_read(uint32_t reg) { return lapic_base[reg / 4]; }

static inline void ioapic_write(uint32_t reg, uint32_t value) {
  ioapic_base[IOREGSEL / 4] = reg;
  ioapic_base[IOWIN / 4] = value;
}

static inline uint32_t ioapic_read(uint32_t reg) {
  ioapic_base[IOREGSEL / 4] = reg;
  return ioapic_base[IOWIN / 4];
}

static inline void set_ioapic_redirection(uint8_t irq, uint8_t vector,
                                          uint8_t apic_id, bool masked) {
  uint32_t reg = IOREDTBL + (irq * 2);
  uint32_t low = vector;
  uint32_t high = apic_id << 24;

  if (masked) {
    low |= (1 << 16);  // set mask bit
  }

  ioapic_write(reg, low);
  ioapic_write(reg + 1, high);
}

void lapic_eoi(void) { lapic_write(EOI, 0); }

// disable the legacy PIC
void disable_pic(void) {
  outb(MASTER_PIC_DATA, 0xFF);  // Mask all IRQs on master
  outb(SLAVE_PIC_DATA, 0xFF);  // Mask all IRQs on slave
}

void init_apic(void) {
  // map the LAPIC address
  void* phys_addr = getLocalApicAddr();
  void* virt_addr = phys_to_virt(phys_addr);

  if (!mmap(virt_addr, phys_addr)) {
    LOG_ERROR("Failed to map LAPIC address!");
    return;
  }

  // map the IOAPIC address
  struct madtEntryType1_s* addr = getMADTApicEntry(MADT_I_O_APIC);
  void* ioapic_phys_addr = (void*)(uintptr_t)addr->ioApciAddr;
  void* ioapic_virt_addr = phys_to_virt(ioapic_phys_addr);

  if (!mmap(ioapic_virt_addr, ioapic_phys_addr)) {
    LOG_ERROR("Failed to map IOAPIC address!");
    return;
  }

  lapic_base = virt_addr;
  ioapic_base = ioapic_virt_addr;

  // enable APIC
  lapic_write(SIV, ENABLE_APIC | 0xff);
  lapic_write(TASK_PRIORITY, 0x00);  // set task priority to 0

  disable_pic();

  uint8_t lapic_id = lapic_read(APIC_ID) >> 24;

  set_ioapic_redirection(1, 33, lapic_id, false);

  uint32_t ioapicver = ioapic_read(IOAPICVER);
  uint32_t max_redirection_entries = ((ioapicver >> 16) & 0xFF) + 1;
  LOG_DEBUG("IOAPIC Version: 0x%X, Max Redirection Entries: %u\n",
            ioapicver & 0xFF, max_redirection_entries);
}
