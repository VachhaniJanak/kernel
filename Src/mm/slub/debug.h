#pragma once
#include "slub_utils.h"
#include <stddef.h>

void print_slab_list(struct kmem_slab *p);

void print_kmem_cache(struct kmem_cache *p);

void print_kmem_slab(struct kmem_slab *p);

void print_kmem_obj(struct kmem_obj *p);

void print_hash_tables(HashMap_t *hash);

void *get_page(size_t page_size);

void *free_page(void *ptr, size_t page_size);

size_t get_page_size(void);
