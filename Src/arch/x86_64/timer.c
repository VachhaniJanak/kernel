#include <arch/x86_64/apic.h>
#include <arch/x86_64/timer.h>
#include <drivers/acpi/acpi.h>
#include <mm/mm.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#include "hpet.h"

static const uint64_t FS_PER_SEC = 1000000000000000ULL;
static hpetRegisters_t* hpetRegs = NULL;
static volatile size_t millis_ticks = 0;

size_t get_counter_value(void) { return hpetRegs->mainCounterValue; }

void sleep_millis(size_t milli) {
  size_t curr_tick = get_counter_value();
  while (get_counter_value() - curr_tick <= milli * millis_ticks) {
    __asm__ volatile("pause");
  }
}

void init_timer(void) {
  struct hpet_s* hpet = getHpet();

  if (hpet == NULL) {
    LOG_ERROR("HPET not found!");
    return;
  }

  void* hpetVirtAddr = phys_to_virt((void*)hpet->address);

  if (!mmap(hpetVirtAddr, (void*)hpet->address)) {
    LOG_ERROR("Failed to map HPET registers!");
    return;
  }

  hpetRegs = (hpetRegisters_t*)hpetVirtAddr;

  if (!is_64bit_wide(hpetRegs)) {
    LOG_ERROR("Timer is not 64-bit wid!");
    return;
  }

  // return per tick period in femtosecond
  uint64_t clk_period = get_counter_clk_period(hpetRegs);
  uint64_t ticks_per_sec = FS_PER_SEC / clk_period;
  uint64_t ticks_per_ms = ticks_per_sec / 1000;
  millis_ticks = ticks_per_ms;

  // if (!timn_config(hpetRegs, 0, false, true, true, ticks_per_ms)) {
  //   LOG_ERROR("Faild to initialize timer!");
  //   return;
  // }

  hpetRegs->mainCounterValue = 0;
  enable_main_counter(hpetRegs);
}
