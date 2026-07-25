#include <arch/x86_64/apic.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/timer.h>
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
#define ENABLE_APIC 0x00000100
#define SVECTOR_MASK 0xFF

#define APIC_APICID 0x20
#define APIC_APICVER 0x30
#define APIC_TASKPRIOR 0x80
#define APIC_EOI 0x0B0
#define APIC_LDR 0x0D0
#define APIC_DFR 0x0E0
#define APIC_SPURIOUS 0x0F0
#define APIC_ESR 0x280
#define APIC_ICRL 0x300
#define APIC_ICRH 0x310

#define APIC_LVT_TMR 0x320
#define APIC_LVT_THML 0x330
#define APIC_LVT_PERF 0x340
#define APIC_LVT_LINT0 0x350
#define APIC_LVT_LINT1 0x360
#define APIC_LVT_ERR 0x370

#define APIC_TMRINITCNT 0x380
#define APIC_TMRCURRCNT 0x390
#define APIC_TMRDIV 0x3E0
#define APIC_LAST 0x38F

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

void lapic_eoi(void) { lapic_write(APIC_EOI, 0); }

// disable the legacy PIC
void disable_pic(void) {
  outb(MASTER_PIC_DATA, 0xFF);  // Mask all IRQs on master
  outb(SLAVE_PIC_DATA, 0xFF);   // Mask all IRQs on slave
}

static inline uint32_t lvt_tim_entry(uint8_t vector, bool mask,
                                     bool is_periodic) {
  uint32_t value = 0;
  value |= vector;

  if (mask) {
    value |= 1 << 16;
  }

  if (is_periodic) {
    value |= 1 << 17;
  }

  return value;
}

uint32_t get_apic_timer_value(void) { return lapic_read(APIC_TMRCURRCNT); }

static inline size_t calib_apic_timer(void) {
  const size_t sample_size = 8;
  size_t samples[sample_size];

  for (size_t i = 0; i < sample_size; i++) {
    lapic_write(APIC_TMRINITCNT, 0xFFFFFFFF);
    sleep_millis(10);
    samples[i] = 0xFFFFFFFF - get_apic_timer_value();
  }

  size_t sum = 0;

  for (size_t i = 0; i < sample_size; i++) {
    sum += samples[i];
  }

  return sum / sample_size;
}

void init_apic_timer(void) {
  lapic_write(APIC_TMRDIV, 0x0);
  lapic_write(APIC_TMRINITCNT, 0xFFFFFFFF);
  size_t ticks_in_10ms = calib_apic_timer();

  lapic_write(APIC_TMRINITCNT, ticks_in_10ms);
  lapic_write(APIC_LVT_TMR, lvt_tim_entry(32, false, true));
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
  lapic_write(APIC_SPURIOUS, ENABLE_APIC | 0xff);
  lapic_write(APIC_TASKPRIOR, 0x00);  // set task priority to 0
  disable_pic();

  uint8_t lapic_id = lapic_read(APIC_APICID) >> 24;

  // Set up IOAPIC redirection for keyboard IRQ (IRQ1) to vector 33
  set_ioapic_redirection(1, 33, lapic_id, false);

#ifdef APIC_DEBUG
  uint32_t ioapicver = ioapic_read(IOAPICVER);
  uint32_t max_redirection_entries = ((ioapicver >> 16) & 0xFF) + 1;
  LOG_DEBUG("IOAPIC Version: 0x%X, Max Redirection Entries: %u\n",
            ioapicver & 0xFF, max_redirection_entries);
#endif

  init_apic_timer();
}
