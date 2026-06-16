#pragma once
#include <stdint.h>

void map_page(void *virt_addr, void *phys_addr, uint64_t flags,
              void *(*phys_to_virt)(void *));

void *unmap_page(void *virt_addr, void *(*phys_to_virt)(void *),
                 void *(*virt_to_phys)(void *));
