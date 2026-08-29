#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
  uint64_t timerConfigAndCap;
  uint64_t timerComparatorValue;
  uint64_t timerFsbInterruptRoute;
  uint64_t reserved;
} hpetTimerRegisters_t;

typedef struct __attribute__((packed)) {
  uint64_t generalCapabilities;
  uint64_t reserved0;
  uint64_t generalConfiguration;
  uint64_t reserved1;
  uint64_t generalInterruptStatus;
  uint8_t reserved2[200];
  uint64_t mainCounterValue;
  uint64_t reserved3;
  hpetTimerRegisters_t timers[3];
  uint8_t reserved_4[672];
} hpetRegisters_t;

static inline uint32_t get_counter_clk_period(hpetRegisters_t* ptr) {
  uint64_t reg = ptr->generalCapabilities;
  return (reg >> 32) & 0xFFFFFFFF;
}

static inline bool is_64bit_wide(hpetRegisters_t* ptr) {
  uint64_t reg = ptr->generalCapabilities;
  return ((reg >> 13) & 0x1) == 0x1;
}

static inline size_t get_num_tim(hpetRegisters_t* ptr) {
  uint64_t reg = ptr->generalCapabilities;
  return ((reg >> 8) & 0x1F) + 1;
}

static inline void enable_main_counter(hpetRegisters_t* ptr) {
  ptr->generalConfiguration |= 0x1;
}

static inline bool is_timx_int_active(hpetRegisters_t* ptr, size_t tim) {
  uint64_t reg = ptr->generalInterruptStatus;
  return ((reg >> tim) & 0x1) == 0x1;
}

static inline bool is_timn_64bit(hpetTimerRegisters_t* tim) {
  uint64_t reg = tim->timerConfigAndCap;
  return ((reg >> 5) & 0x1) == 0x1;
}

static inline bool is_per_int_cap(hpetTimerRegisters_t* tim) {
  uint64_t reg = tim->timerConfigAndCap;
  return ((reg >> 4) & 0x1) == 0x1;
}

static inline size_t timn_int_route(hpetTimerRegisters_t* tim, int skip) {
  uint64_t reg = tim->timerConfigAndCap >> 32;

  for (size_t i = 0; i < 32; i++) {
    if ((reg >> i) & 0x1) {
      if (skip) {
        skip--;
        continue;
      }
      return i;
    }
  }

  return 64;
}

static inline size_t get_timn_int_route(hpetTimerRegisters_t* tim) {
  uint64_t reg = tim->timerConfigAndCap >> 32;
  return reg;
}

bool timn_config(hpetRegisters_t* ptr, size_t tim, size_t route_pin,
                 bool is_ltrig, bool enable_int, bool is_periodic,
                 uint64_t value);