#include <arch/x86_64/mmu.h>
#include <kernel.h>
#include <mm/pmm/pmm.h>
#include <mm/slub/slub.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

static struct sslub_state_s sslub_state = {0};
static struct lslub_state_s lslub_state = {0};
static struct mm_state_s* mm_state = NULL;

void* khalloc_page(void);
void khfree_page(void* addr);

void init_kheap(struct mm_state_s* state) {
  if (state == NULL) return;

  mm_state = state;
  sslub_state.page_size = mm_state->page_size;
  sslub_state.get_page = &khalloc_page;
  sslub_state.free_page = &khfree_page;

  init_slub_scaches(&sslub_state);

  lslub_state.page_size = mm_state->page_size;
  lslub_state.get_page = &khalloc_page;
  lslub_state.free_page = &khfree_page;

  init_slub_lcaches(&lslub_state);
  state->heap_state.cursor = (void*)state->kernel_heap_base;
  state->heap_state.fragment_list = NULL;
}

void* kmalloc(size_t size) {
  if (size == 0) return NULL;

  if (size <= MAX_SIZE_SMALL_OBJECTS) return sslub_alloc(&sslub_state, size);

  if (size <= MAX_SIZE_LARGE_OBJECTS) return lslub_alloc(&lslub_state, size);

  return NULL;
}

void kfree(void* ptr) {
  if (ptr == NULL) return;

  if (sslub_free(&sslub_state, ptr)) return;

  if (lslub_free(&lslub_state, ptr)) return;
}

static inline void push(void* addr) {
  struct sgl_node_s* n = pre_obj_alloc(sizeof(struct sgl_node_s));

  if (n == NULL) return;

  n->addr = addr;
  n->size = mm_state->page_size;

  n->next = mm_state->heap_state.fragment_list;
  mm_state->heap_state.fragment_list = n;
}

static inline void* pop(void) {
  struct sgl_node_s* n = mm_state->heap_state.fragment_list;

  if (n == NULL) return NULL;

  mm_state->heap_state.fragment_list = n->next;
  void* addr = n->addr;

  pre_obj_free(n);

  return addr;
}

static inline uintptr_t get_end_addr(void* addr, size_t page_size) {
  return (uintptr_t)addr + page_size;
}

static inline void* kheap_vaddr(void) {
  void* addr = pop();

  if (addr != NULL) return addr;

  size_t total_size = mm_state->kernel_heap_size;
  uintptr_t base_addr = mm_state->kernel_heap_base;
  total_size -= ((uintptr_t)mm_state->heap_state.cursor - base_addr);

  if (mm_state->page_size > total_size) return NULL;

  addr = mm_state->heap_state.cursor;
  mm_state->heap_state.cursor = (void*)get_end_addr(addr, mm_state->page_size);

  return addr;
}

static inline void kheap_vfree(void* addr) {
  void* end_addr = (void*)get_end_addr(addr, mm_state->page_size);

  if (end_addr == mm_state->heap_state.cursor) {
    mm_state->heap_state.cursor = addr;
    return;
  }

  push(addr);
}

void* khalloc_page(void) {
  mm_flags_t flags = MM_FLAG_WRITABLE | MM_FLAG_READ;
  const size_t page_size = mm_state->page_size;
  uint8_t* phy_addr = pmm_alloc(page_size);
  void* root_table = mm_get_root_table();

  if (phy_addr == NULL) return NULL;

  uint8_t* vir_addr = kheap_vaddr();

  if (vir_addr == NULL) {
    pmm_free(phy_addr);
    return NULL;
  }

  mm_result_t result = map_page(root_table, vir_addr, phy_addr, flags);

  if (result != MM_SUCCESS) {
    pmm_free(phy_addr);
    kheap_vfree(vir_addr);
    return NULL;
  }

  return vir_addr;
}

void khfree_page(void* addr) {
  if (addr == NULL) return;

  void* root_table = mm_get_root_table();

  uintptr_t phys_addr;
  mm_result_t result = unmap_page(root_table, addr, &phys_addr);

  if (result == MM_SUCCESS) pmm_free((void*)phys_addr);

  kheap_vfree(addr);
}

void kheap_debug(void) {
  LOG_NEWLINE();
  LOG_DEBUG("KHEAP:\n");

  struct sgl_node_s* n = mm_state->heap_state.fragment_list;
  while (n != NULL) {
    LOG_PRINT("[addr: %p, size: %zu] -> ", n->addr, n->size);
    n = n->next;
  }
  LOG_PRINT("NULL\n");
}

void* kmalloc_phys_addr(void* virt_addr) {
  if (virt_addr == NULL) return NULL;

  uintptr_t phys_addr;
  mm_result_t result = get_mapping(mm_get_root_table(), virt_addr, &phys_addr);
  if (result != MM_SUCCESS) return NULL;

  return (void*)phys_addr;
}