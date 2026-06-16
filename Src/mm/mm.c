#include "debug.h"
#include "mmutils.h"
#include "pmm.h"
#include "vmm.h"
#include <arch/x86_64/mmu.h>
#include <boot/boot.h>
#include <kernel.h>
#include <mm/mm.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

uintptr_t hhdm_offset;

static inline void *hhdm_virt_to_phys(void *ptr) {
  return (void *)((uintptr_t)ptr - hhdm_offset);
}

static inline void *hhdm_phys_to_virt(void *ptr) {
  return (void *)((uintptr_t)ptr + hhdm_offset);
}

int mm_init(void) {

  const size_t noEntries = getMMapEntryCount();
  struct MemoryMapEntry_s entries[noEntries];

  if (!copyMMapEntry(entries)) {
    LOG_ERROR("[MM] Failed to copy memory map entries.");
    return -1;
  }

  hhdm_offset = getHHDMOffset();

  if (hhdm_offset == 0) {
    LOG_ERROR("[MM] Failed to get HHDM offset.");
    return -1;
  }

  size_t max_usable_length;
  uintptr_t max_usable_base;

  get_max_len(entries, noEntries, MEMMAP_USABLE, &max_usable_length,
              &max_usable_base);

  // check memory address is aligned to page size
  if (max_usable_base % MM_DEFAULT_PAGE_SIZE != 0) {
    LOG_ERROR(
        "[MM] Max usable memory base address is not aligned to page size.");
    return -1;
  }

  // init physical allocator
  if (!init_pmm(max_usable_base, max_usable_length, MM_DEFAULT_PAGE_SIZE,
                hhdm_phys_to_virt)) {
    LOG_ERROR("[MM] Buddy initialization failed!");
    return -1;
  }

#ifdef DEBUG
  LOG_NEWLINE();
  LOG_DEBUG("HHDM Offset: 0x%lx\n", hhdm_offset);
  print_kernel_addr();
#endif

  return 0;
}

// void vmalloc_init(struct vm_state *state) {
//   state->cursor = (void *)KERNEL_VMALLOC_BASE;
//   state->total_size = KERNEL_VMALLOC_SIZE;
//   state->page_size = MM_DEFAULT_PAGE_SIZE;
// }

// static inline size_t available_space(struct vm_state *state) {
//   return state->total_size - ((uintptr_t)state->cursor -
//   KERNEL_VMALLOC_BASE);
// }

// static void *vmalloc_vaddr(struct vm_state *state, size_t size) {

//   if (state == NULL)
//     return NULL;

//   if (size > available_space(state))
//     return NULL;

//   void *addr = state->cursor;
//   state->cursor = (void *)get_end_addr(addr, size);
//   return addr;
// }

// static void vmfree_vaddr(void) { return; }

// void *vmalloc(size_t size, uint64_t flags) {

//   if (size == 0)
//     return NULL;

//   size = round_to_page_size(size, vmalloc_state.page_size);
//   const size_t page_size = MM_DEFAULT_PAGE_SIZE;
//   const size_t no_pages = size / page_size;

//   if (!pmm_pages_avaliable(no_pages))
//     return NULL;

//   uint8_t *phy_addr = pmm_alloc(size);
//   uint8_t *vir_addr = vmalloc_vaddr(&vmalloc_state, size);

//   if (vir_addr == NULL) {
//     pmm_free(phy_addr);
//     return NULL;
//   }

//   // check for continues pages
//   if (phy_addr != NULL) {
//     for (size_t i = 0; i < no_pages; i++) {
//       uint8_t *p_addr = phy_addr + page_size * i;
//       uint8_t *v_addr = vir_addr + page_size * i;
//       map_page(v_addr, p_addr, flags);
//     }
//     return vir_addr;
//   }

//   // if not then allocate non continues physical pages
//   for (size_t i = 0; i < no_pages; i++) {
//     uint8_t *v_addr = vir_addr + page_size * i;
//     void *p_addr = pmm_alloc(page_size);

//     if (p_addr != NULL) {
//       map_page(v_addr, p_addr, flags);
//       continue;
//     }

//     // rollback
//     for (size_t j = 0; j < i; j++) {
//       uint8_t *v_addr = vir_addr + page_size * j;

//       void *p_addr = unmap_page(v_addr);
//       if (p_addr != NULL)
//         pmm_free(p_addr);
//     }
//     vmfree_vaddr();
//   }

//   return vir_addr;
// }

// void vfree(void *addr) {

//   if (addr == NULL)
//     return;

//   pmm_free(addr);
//   vmfree_vaddr();
// }
