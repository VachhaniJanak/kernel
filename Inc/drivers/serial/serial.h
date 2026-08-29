#pragma once

#include <stddef.h>

void serial_init(void);

int serial_printf(const char* format, ...);

size_t serial_write(const char* buffer, size_t length);
