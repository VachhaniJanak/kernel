#include "hpet.h"

#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

bool timn_config(hpetRegisters_t* ptr, size_t tim, bool is_ltrig,
                 bool enable_int, bool is_periodic, uint64_t value) {
  hpetTimerRegisters_t* tim_ptr = &ptr->timers[tim];

  if (!is_timn_64bit(tim_ptr)) {
    LOG_ERROR("Timer is not 64-bit!");
    return false;
  }

  uint64_t val = 0;

  if (is_ltrig) {
    val |= 1 << 1;
  }

  if (enable_int) {
    val |= 1 << 2;
  }

  if (is_periodic) {
    if (!is_per_int_cap(tim_ptr)) {
      LOG_ERROR("Periodic interrupt is not supprted!");
      return false;
    }
    val |= (1 << 3) | (1 << 6);
  }

  size_t route_pin = timn_int_route(tim_ptr, tim + 1);

  if (route_pin > 32) {
    LOG_ERROR("Timer route pin not found!");
    return false;
  }

  val |= (route_pin & 0x1f) << 9;
  tim_ptr->timerConfigAndCap |= val;
  tim_ptr->timerComparatorValue = value;

  return true;
}
