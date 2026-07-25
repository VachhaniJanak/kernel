#pragma once

#include <stdint.h>

#include "gpt.h"

uint32_t calculate_crc32(const uint8_t* data, const uint32_t length);

void print_primary_header(gpt_primary_header_t* header);

void print_protective_mbr(protective_mbr_t* sector);

void print_partition_entry(gpt_partition_entry_t* entries);
