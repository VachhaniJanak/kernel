#pragma once

#include <stddef.h>

// forward declaration
struct pmm_state_s;

void kmalloc_init(struct pmm_state_s* pmm);

void* kmalloc(size_t size);

void kfree(void* ptr);
