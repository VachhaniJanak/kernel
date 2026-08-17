#include <arch/x86_64/mmu.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/slub/slub.h>
#include <mm/vmm/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#include "../debug.h"
#include "rbtree.h"

void* valloc_page(void);
void vfree_page(void* addr);

static RBTree vmm_tree = {0};
static struct mm_state_s* mm_state = NULL;
static struct sslub_state_s pre_alloc = {0};
static struct sgl_node_s* size_classes[50] = {NULL};

bool init_vmm(struct mm_state_s* state) {
  if (state == NULL) return false;

  mm_state = state;
  rb_create(&vmm_tree);

  mm_state->vmalloc_state.cursor = (void*)mm_state->kernel_vmalloc_base;
  mm_state->vmalloc_state.fragment_list = size_classes;

  // initialize small caches for slab allocator
  // for node allocation e.g: red-black nodes

  pre_alloc.page_size = mm_state->page_size;
  pre_alloc.get_page = &valloc_page;
  pre_alloc.free_page = &vfree_page;

  init_slub_scaches(&pre_alloc);

  return true;
}

void* pre_obj_alloc(size_t size) { return sslub_alloc(&pre_alloc, size); }

void pre_obj_free(void* ptr) { sslub_free(&pre_alloc, ptr); }

bool map_page(void* virt_addr, void* phys_addr, uint64_t flags,
              void* (*phys_to_virt)(void*)) {
  uint64_t* current_table = (uint64_t*)get_page_table_addr();
  current_table = phys_to_virt(current_table);

  size_t pml4_idx = PML4_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);

  if ((current_table[pml4_idx] == 0) ||
      !(current_table[pml4_idx] & MMU_PRESENT)) {
    uint64_t* addr = pmm_alloc(PDPT_SIZE);

    if (addr == NULL) return false;

    kmemset(phys_to_virt(addr), 0, PDPT_SIZE);
    addr = (uint64_t*)((uint64_t)addr & PAGE_MASK);
    current_table[pml4_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE | flags;
  }

  size_t pdpt_idx = PDPT_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pml4_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pdpt_idx] == 0) ||
      !(current_table[pdpt_idx] & MMU_PRESENT)) {
    uint64_t* addr = pmm_alloc(PAGE_DIRECTORY_SIZE);

    if (addr == NULL) return false;

    kmemset(phys_to_virt(addr), 0, PAGE_DIRECTORY_SIZE);
    addr = (uint64_t*)((uint64_t)addr & PAGE_MASK);
    current_table[pdpt_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE | flags;
  }

  size_t pd_idx = PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pdpt_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pd_idx] == 0) || !(current_table[pd_idx] & MMU_PRESENT)) {
    uint64_t* addr = pmm_alloc(PAGE_TABLE_SIZE);

    if (addr == NULL) return false;

    kmemset(phys_to_virt(addr), 0, PAGE_TABLE_SIZE);
    addr = (uint64_t*)((uint64_t)addr & PAGE_MASK);
    current_table[pd_idx] = (uint64_t)addr | MMU_PRESENT | MMU_WRITABLE | flags;
  }

  size_t pt_idx = PAGE_TABLE_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pd_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);
  phys_addr = (uint64_t*)((uint64_t)phys_addr & PAGE_MASK);
  current_table[pt_idx] = ((uint64_t)phys_addr) | flags;

  return true;
}

static inline bool all_page_entry_free(uint64_t* addr, size_t entries) {
  for (size_t i = 0; i < entries; i++)
    if ((addr[i] != 0) && (addr[i] & MMU_PRESENT)) return false;
  return true;
}

void* unmap_page(void* virt_addr, void* (*phys_to_virt)(void*),
                 void* (*virt_to_phys)(void*)) {
  uint64_t* current_table = (uint64_t*)get_page_table_addr();
  current_table = phys_to_virt(current_table);
  size_t pml4_idx = PML4_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);

  uint64_t* page_stack[4];
  size_t idx = 0;

  tlb_invalided(virt_addr);

  if ((current_table[pml4_idx] == 0) ||
      !(current_table[pml4_idx] & MMU_PRESENT)) {
    return NULL;
  }

  page_stack[idx++] = current_table;

  size_t pdpt_idx = PDPT_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pml4_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pdpt_idx] == 0) ||
      !(current_table[pdpt_idx] & MMU_PRESENT)) {
    return NULL;
  }

  page_stack[idx++] = current_table;

  size_t pd_idx = PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pdpt_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pd_idx] == 0) || !(current_table[pd_idx] & MMU_PRESENT))
    return NULL;

  page_stack[idx++] = current_table;

  size_t pt_idx = PAGE_TABLE_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pd_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  page_stack[idx++] = current_table;

  void* phys_addr = (void*)current_table[pt_idx];
  phys_addr = (void*)((uint64_t)phys_addr & PAGE_MASK);
  current_table[pt_idx] = 0;

  // check if all entries in table is free then remove
  if (!all_page_entry_free(page_stack[--idx], 512)) return phys_addr;

  page_stack[idx - 1][pd_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  if (!all_page_entry_free(page_stack[--idx], 512)) return phys_addr;

  page_stack[idx - 1][pdpt_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  if (!all_page_entry_free(page_stack[--idx], 512)) return phys_addr;

  page_stack[idx - 1][pml4_idx] = 0;
  pmm_free(virt_to_phys(page_stack[idx]));

  return phys_addr;
}

void* map_phys_addr(void* virt_addr, void* (*phys_to_virt)(void*)) {

  uint64_t* current_table = (uint64_t*)get_page_table_addr();
  current_table = phys_to_virt(current_table);
  size_t pml4_idx = PML4_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);

  if ((current_table[pml4_idx] == 0) ||
      !(current_table[pml4_idx] & MMU_PRESENT)) {
    return NULL;
  }

  size_t pdpt_idx = PDPT_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pml4_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pdpt_idx] == 0) ||
      !(current_table[pdpt_idx] & MMU_PRESENT)) {
    return NULL;
  }

  size_t pd_idx = PAGE_DIRECTORY_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pdpt_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  if ((current_table[pd_idx] == 0) || !(current_table[pd_idx] & MMU_PRESENT))
    return NULL;

  size_t pt_idx = PAGE_TABLE_ADDR_TO_ENTRY_INDEX((uintptr_t)virt_addr);
  current_table = (uint64_t*)current_table[pd_idx];
  current_table = (uint64_t*)((uint64_t)current_table & PAGE_MASK);
  current_table = phys_to_virt(current_table);

  void* phys_addr = (void*)current_table[pt_idx];
  phys_addr = (void*)((uint64_t)phys_addr & PAGE_MASK);

  return phys_addr;
}

static inline size_t size_to_class_index(size_t size, size_t page_size) {
  if (size == 0) return 0;

  return log2(size) - log2(page_size);
}

static inline void push(void* addr, size_t size) {
  struct sgl_node_s* n = pre_obj_alloc(sizeof(struct sgl_node_s));

  if (n == NULL) return;

  n->addr = addr;
  n->size = size;

  size_t idx = size_to_class_index(size, mm_state->page_size);
  struct sgl_node_s** list = mm_state->vmalloc_state.fragment_list;
  n->next = list[idx];
  list[idx] = n;
}

static inline void* pop(size_t size) {
  size_t idx = size_to_class_index(size, mm_state->page_size);
  struct sgl_node_s** list = mm_state->vmalloc_state.fragment_list;
  struct sgl_node_s* n = list[idx];

  if (n == NULL) return NULL;

  size_classes[idx] = n->next;
  void* addr = n->addr;

  pre_obj_free(n);

  return addr;
}

static inline void* vmalloc_vaddr(size_t size) {
  void* addr = pop(size);

  if (addr != NULL) return addr;

  size_t total_size = mm_state->kernel_vmalloc_size;
  uintptr_t base_addr = mm_state->kernel_vmalloc_base;
  total_size -= ((uintptr_t)mm_state->vmalloc_state.cursor - base_addr);

  if (size > total_size) return NULL;

  addr = mm_state->vmalloc_state.cursor;
  mm_state->vmalloc_state.cursor = (void*)get_end_addr(addr, size);

  return addr;
}

static inline void vfree_vaddr(void* addr, size_t size) {
  void* end_addr = (void*)get_end_addr(addr, size);

  if (end_addr == mm_state->vmalloc_state.cursor) {
    mm_state->vmalloc_state.cursor = addr;
    return;
  }

  push(addr, size);
}

void* valloc_page(void) {
  const uint64_t flags = MMU_PRESENT | MMU_WRITABLE;
  const size_t page_size = mm_state->page_size;
  uint8_t* phy_addr = pmm_alloc(page_size);

  if (phy_addr == NULL) return NULL;

  uint8_t* vir_addr = vmalloc_vaddr(page_size);

  if (vir_addr == NULL) {
    pmm_free(phy_addr);
    return NULL;
  }

  if (!map_page(vir_addr, phy_addr, flags, phys_to_virt)) {
    pmm_free(phy_addr);
    vfree_vaddr(vir_addr, page_size);
    return NULL;
  }

  return vir_addr;
}

void vfree_page(void* addr) {
  if (addr == NULL) return;

  void* phys_addr = unmap_page(addr, phys_to_virt, virt_to_phys);

  if (phys_addr != NULL) pmm_free(phys_addr);

  vfree_vaddr(addr, mm_state->page_size);
}

void* vmalloc(size_t size, uint64_t flags, bool continuous) {
  if (size == 0) return NULL;

  const size_t page_size = mm_state->page_size;
  size = round_to_page_size(size, page_size);
  const size_t no_pages = size / page_size;

  if (!pmm_pages_avaliable(no_pages)) return NULL;

  uint8_t* phy_addr = pmm_alloc(size);
  uint8_t* vir_addr = vmalloc_vaddr(size);

  if (vir_addr == NULL) {
    pmm_free(phy_addr);
    return NULL;
  }

  // check for continues pages
  if (phy_addr != NULL) {
    for (size_t i = 0; i < no_pages; i++) {
      uint8_t* p_addr = phy_addr + page_size * i;
      uint8_t* v_addr = vir_addr + page_size * i;

      map_page(v_addr, p_addr, flags, phys_to_virt);
    }

    rb_insert(&vmm_tree, vir_addr, size, true);
    return vir_addr;
  }

  if (continuous) return NULL;

  // if not, then allocate non continues physical pages
  for (size_t i = 0; i < no_pages; i++) {
    uint8_t* v_addr = vir_addr + page_size * i;
    void* p_addr = pmm_alloc(page_size);

    if (p_addr != NULL) {
      map_page(v_addr, p_addr, flags, phys_to_virt);
      continue;
    }

    // rollback
    for (size_t j = 0; j < i; j++) {
      uint8_t* v_addr = vir_addr + page_size * j;

      void* p_addr = unmap_page(v_addr, phys_to_virt, virt_to_phys);
      if (p_addr != NULL) pmm_free(p_addr);
    }
    vfree_vaddr(vir_addr, size);
  }

  rb_insert(&vmm_tree, vir_addr, size, false);
  return vir_addr;
}

void vfree(void* addr) {
  if (addr == NULL) return;

  size_t size;
  bool is_continuous;

  if (!rb_delete(&vmm_tree, addr, &size, &is_continuous)) return;

  vfree_vaddr(addr, size);

  const size_t page_size = mm_state->page_size;
  const size_t no_pages = size / page_size;

  if (is_continuous) {
    void* phys_addr = unmap_page(addr, phys_to_virt, virt_to_phys);

    for (size_t i = 1; i < no_pages; i++) {
      uint8_t* v_addr = (uint8_t*)addr + page_size * i;
      unmap_page(v_addr, phys_to_virt, virt_to_phys);
    }

    if (phys_addr != NULL) pmm_free(phys_addr);

    return;
  }

  for (size_t i = 0; i < no_pages; i++) {
    uint8_t* v_addr = (uint8_t*)addr + page_size * i;
    void* p_addr = unmap_page(v_addr, phys_to_virt, virt_to_phys);

    if (p_addr != NULL) pmm_free(p_addr);
  }
}

/////////////////////////// DEBUGGING FUNCTIONS ///////////////////////////

#ifdef DEBUG

/* Validation (debug)  */
/* Traversals  */

void inorder(RBTree* t, Node* x) {
  if (x == t->nil) return;

  inorder(t, x->left);
  LOG_PRINT("%p(%s)\n", x->addr, x->color == RED ? "R" : "B");
  inorder(t, x->right);
}

/* Returns black-height; -1 on violation */
static int validate_helper(RBTree* t, Node* x) {
  if (x == t->nil) return 1;

  if (x->color == RED) {
    if (x->left->color == RED) return -1;
    if (x->right->color == RED) return -1;
  }
  int lh = validate_helper(t, x->left);
  int rh = validate_helper(t, x->right);
  if (lh == -1 || rh == -1 || lh != rh) return -1;
  return lh + (x->color == BLACK ? 1 : 0);
}

int rb_validate(RBTree* t) {
  if (t->root->color != BLACK) return 0;
  return validate_helper(t, t->root) != -1;
}

void print_size_classes(void) {
  for (size_t i = 0; i < sizeof(size_classes) / sizeof(size_classes[0]); i++) {
    struct sgl_node_s* n = size_classes[i];

    if (n == NULL) continue;

    LOG_PRINT("Size class %2zu: ", i);
    while (n != NULL) {
      LOG_PRINT("[addr: %p, size: %zu] -> ", n->addr, n->size);
      n = n->next;
    }
    LOG_PRINT("NULL\n");
  }
}

void debug_print_vmm_tree(void) {
  LOG_NEWLINE();
  LOG_DEBUG("=== VMM Tree :\n");
  LOG_PRINT("=== In-order :\n");
  inorder(&vmm_tree, vmm_tree.root);
  LOG_PRINT("=== Valid RB tree? %s\n", rb_validate(&vmm_tree) ? "YES" : "NO");

  // for (size_t i = 0; i < NO_SMALL_PRE_CACHES; i++)
  // print_slub_kmem_cache(&pre_alloc.caches[i]);

  print_slub_kmem_cache(&pre_alloc.caches[5]);
}

void debug_print_size_classes(void) {
  LOG_NEWLINE();
  LOG_DEBUG("=== Size Classes :\n");
  print_size_classes();
}

#endif
