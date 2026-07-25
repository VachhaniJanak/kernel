#pragma once

#include <stdint.h>
#include <stddef.h>

void init_disk(void);

void read_dev_disk(uint8_t* buff, size_t sector, size_t count);

void write_dev_disk(const uint8_t* buff, size_t sector, size_t count);
