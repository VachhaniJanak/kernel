#pragma once

#include <stdint.h>
#include <stddef.h>

void init_apic(void);

void lapic_eoi(void);

uint32_t get_apic_timer_value(void);

void init_apic_timer(void);

size_t apic_get_id(void);
