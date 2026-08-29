#pragma once

#include <stddef.h>

#include "hpet.h"

typedef uint64_t timer_tick_t;

extern hpetRegisters_t* timer_hpet_reg;
extern volatile size_t timer_ticks_per_ms;
extern volatile size_t timer_sec_ticks;

void timer_init(void);

void timer_sleep_ms(size_t milli);

size_t timer_get_irq_pin(void);

static inline timer_tick_t timer_get_sec_ticks(void) { return timer_sec_ticks; }

static inline timer_tick_t timer_get_ticks(void) {
  return timer_hpet_reg->mainCounterValue;
}

static inline timer_tick_t timer_ms_to_ticks(size_t milli) {
  return milli * timer_ticks_per_ms;
}

static inline timer_tick_t timer_get_delta_tick(timer_tick_t start,
                                                timer_tick_t end) {
  // c unsigned arithmetic already wraps modulo 2^64
  return end - start;
}
