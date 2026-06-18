#pragma once

#include <mm/mm.h>

void init_kheap(struct mm_state_s *state);

void *kmalloc(size_t size);

void kfree(void *ptr);

void kheap_debug(void);

void *khalloc_page(void);

void khfree_page(void *addr);