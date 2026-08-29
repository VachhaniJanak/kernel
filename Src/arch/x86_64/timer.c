#include <arch/x86_64/apic.h>
#include <arch/x86_64/hpet.h>
#include <arch/x86_64/timer.h>
#include <drivers/acpi/acpi.h>
#include <mm/mm.h>
#include <stdint.h>
#include <utils/log.h>

// #define TIMER_DEBUG

hpetRegisters_t* timer_hpet_reg = NULL;
volatile size_t timer_ticks_per_ms = 0;  // Number of HPET ticks per millisecond
volatile size_t timer_sec_ticks = 0;

// 1 millisecond = 1e6 nanoseconds = 1e12 femtoseconds
static const uint64_t fs_per_ms = 1e12;

void apic_timer_irq_isr_handler(void) {
  timer_sec_ticks++;
  lapic_eoi();
}

// This function provides a busy-wait sleep for the specified number of
// milliseconds.
void timer_sleep_ms(size_t milli) {
  timer_tick_t curr_tick = timer_get_ticks();

  while (timer_get_ticks() - curr_tick <= milli * timer_ticks_per_ms) {
    __asm__ volatile("pause");
  }
}

size_t timer_get_irq_pin(void) {
  hpetTimerRegisters_t* tim_ptr = &timer_hpet_reg->timers[0];
  size_t route_pin = timn_int_route(tim_ptr, 1);
  return route_pin;
}

void timer_init(void) {
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

  timer_hpet_reg = (hpetRegisters_t*)hpetVirtAddr;

  if (!is_64bit_wide(timer_hpet_reg)) {
    LOG_ERROR("Timer is not 64-bit wide!");
    return;
  }

  // return per tick period in femtosecond
  uint64_t clk_period = get_counter_clk_period(timer_hpet_reg);

  // Calculate the number of ticks per millisecond
  uint64_t ticks_per_ms = fs_per_ms / clk_period;
  uint64_t ticks_per_sec = 1000 * ticks_per_ms;
  timer_ticks_per_ms = ticks_per_ms;

  size_t route_pin = timer_get_irq_pin();

  if (!timn_config(timer_hpet_reg, 0, route_pin, false, true, true,
                   ticks_per_sec)) {
#ifdef TIMER_DEBUG
    log_error("Failed to initialize timer!");
#endif
    return;
  }

#ifdef TIMER_DEBUG
  log_print("HPET initialized:\n");
  log_print("  HPET Address: 0x%lx\n", (uintptr_t)hpetVirtAddr);
  log_print("  Clock Period: %lu femtoseconds\n", clk_period);
  log_print("  Ticks per Millisecond: %lu\n", ticks_per_ms);
  log_print("  Ticks per Second: %lu\n", ticks_per_sec);
  log_print("  Routing Pin: ");

  for (size_t i = 0; i < 32; i++) {
    size_t route_pins = get_timn_int_route(&timer_hpet_reg->timers[0]);

    if ((route_pins >> i) & 0x1) {
      log_print("%zu, ", i);
    }
  }

  log_newline();
#endif

  timer_hpet_reg->mainCounterValue = 0;
  enable_main_counter(timer_hpet_reg);
}
