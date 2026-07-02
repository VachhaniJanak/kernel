#pragma once

#include <stdint.h>

void init_apic(void);

void lapic_eoi(void);

uint32_t get_apic_timer_value(void);
