#pragma once

#include <mm/pmm/buddy.h>
#include <mm/slub/slub.h>

void print_buddy_state(buddy_t *buddy);

void print_kernel_addr(void);

void print_slab_list(struct kmem_slab *p);

void print_slub_kmem_cache(struct kmem_cache *p);

void print_slub_kmem_slab(struct kmem_slab *p);

void print_slub_kmem_obj(struct kmem_obj *p);

