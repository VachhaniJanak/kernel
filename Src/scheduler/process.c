#include "process.h"

#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <kernel.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/vmm/kheap.h>
#include <mm/vmm/vmm.h>
#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>
#include <vfs/vfs.h>

static inline size_t max(size_t a, size_t b) { return (a > b) ? a : b; }

static inline size_t min(size_t a, size_t b) { return (a < b) ? a : b; }

static inline bool get_intersection(vma_t* vma, void* ptr, uintptr_t* start,
                                    uintptr_t* end) {
  uintptr_t segment_start = (uintptr_t)vma->vm_start;
  uintptr_t segment_end = (uintptr_t)vma->vm_end;

  uintptr_t address_start = (uintptr_t)ptr;
  uintptr_t address_end = address_start + mm_get_page_size();

  *start = max(segment_start, address_start);
  *end = min(segment_end, address_end);

  return *start < *end;
}

static inline mm_flags_t get_vma_flags(uint32_t vma_flags) {
  mm_flags_t flags = 0;

  if (vma_flags & VMA_READ) {
    flags |= MM_FLAG_READ;
  }

  if (vma_flags & VMA_WRITE) {
    flags |= MM_FLAG_WRITABLE;
  }

  if (vma_flags & VMA_EXEC) {
    flags |= MM_FLAG_EXE;
  }

  return flags;
}

static inline bool handle_user_page_fault(uintptr_t faulting_address);

void page_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  uintptr_t faulting_address = page_fault_addr();

#ifdef PAGE_FAULT_DEBUG
  log_debug("Page Fault Exception:\n");
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  Previous RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  log_print("  Current RSP: 0x%lx\n", get_stack_top());
  log_print("  Address: 0x%lx\n", faulting_address);
  log_print("  Error code: 0x%lx\n", frame->error_code);

  if (frame->error_code & MMU_PF_PLV_MASK) {
    log_print("  Page-level protection violation\n");
  } else {
    log_print("  Non-present page\n");
  }

  if (frame->error_code & MMU_PF_WA_MASK) {
    log_print("  Write access\n");
  } else {
    log_print("  Read access\n");
  }

  if (frame->error_code & MMU_PF_UM_MASK) {
    log_print("  User-mode access\n");
  } else {
    log_print("  Kernel-mode access\n");
  }

  log_newline();
#endif

  // handle user space page fault
  if (frame->error_code & MMU_PF_UM_MASK &&
      !(frame->error_code & MMU_PF_PLV_MASK)) {
    if (faulting_address < USER_VIRTUAL_BASE) {
#ifdef PAGE_FAULT_DEBUG
      log_error("Segmentation fault: Invalid user space address 0x%lx",
                faulting_address);
#endif
      while (1);
    }

    if (handle_user_page_fault(faulting_address)) {
#ifdef PAGE_FAULT_DEBUG
      log_info("Handled user page fault at address 0x%lx", faulting_address);
#endif
      return;
    }
#ifdef PAGE_FAULT_DEBUG
    log_error("Failed to handle user page fault at address 0x%lx",
              faulting_address);
#endif
  }

  while (1);
}

static inline bool handle_user_page_fault(uintptr_t faulting_address) {
#ifdef PAGE_FAULT_DEBUG
  log_debug("User Page Fault Handler:\n");
#endif
  const size_t page_size = mm_get_page_size();
  process_t* current_process = scheduler_get_current_process();

  if (current_process == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error("  No current process found while handling page fault");
#endif
    return false;
  }

#ifdef PAGE_FAULT_DEBUG
  log_print("  Current process: '%s' (PID: %zu)\n", current_process->name,
            current_process->pid);
  log_print("  Current thread: '%s' (TID: %zu)\n",
            scheduler_get_current_thread()->name,
            scheduler_get_current_thread()->tid);
#endif

  void* page_start_addr = PAGE_ALIGN_DOWN((void*)faulting_address, page_size);
  void* page_phys_addr = pmm_alloc(page_size);

  if (page_phys_addr == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "Failed to allocate physical page for user page fault at address "
        "0x%lx",
        faulting_address);
#endif
    return false;
  }

  void* buffer = kmalloc(page_size);

  if (buffer == NULL) {
#ifdef PAGE_FAULT_DEBUG
    log_error("Failed to allocate buffer for user page fault at address 0x%lx",
              faulting_address);
#endif
    pmm_free(page_phys_addr);
    return false;
  }

  void* page_virt_addr = phys_to_virt(page_phys_addr);

  // Check if the faulting address falls within any of the process's VMAs
  vma_t* current_vma = current_process->vma_head;
  mm_flags_t flags = MM_FLAG_USER;
  bool is_any_segment_found = false;

#ifdef PAGE_FAULT_DEBUG
  log_print("  Allocating virtual page at address 0x%lx\n",
            (uintptr_t)page_start_addr);
#endif

  while (current_vma != NULL) {
    uintptr_t start, end;

    // Check if the faulting address falls within the current VMA
    if (!get_intersection(current_vma, page_start_addr, &start, &end)) {
      current_vma = current_vma->next;
      continue;
    }

    is_any_segment_found = true;
    size_t overlap_size = end - start;
    flags |= get_vma_flags(current_vma->flags);

#ifdef PAGE_FAULT_DEBUG
    log_print("  Found overlapping VMA:\n");
    log_print("    VMA Start: 0x%lx\n", current_vma->vm_start);
    log_print("    VMA End: 0x%lx\n", current_vma->vm_end);
    log_print("    Overlap Start: 0x%lx\n", start);
    log_print("    Overlap End: 0x%lx\n", end);
    log_print("    Overlap Size: %zu bytes\n", overlap_size);
#endif

    if (current_vma->flags & VMA_FILE_BACKED) {
      size_t file_offset =
          current_vma->file_offset + (start - current_vma->vm_start);

      vfs_seek(current_vma->file, file_offset);
      int ret = vfs_read(current_vma->file, buffer, overlap_size);

      if (ret < 0) {
#ifdef PAGE_FAULT_DEBUG
        log_error(
            "Failed to read from file for file-backed VMA at address 0x%lx",
            faulting_address);
#endif
        kfree(buffer);
        pmm_free(page_phys_addr);
        return false;
      }

      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

#ifdef PAGE_FAULT_DEBUG
      log_print("  File-Backed VMA:\n", ret);
      log_print("    File Offset: 0x%lx\n", file_offset);
      log_print("    Bytes Read: %d\n", ret);
      log_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      log_print("    Overlap Size: %zu bytes\n", overlap_size);
#endif
      kmemcpy(dest_addr, buffer, overlap_size);

    } else if (current_vma->flags & VMA_ANONYMOUS) {
      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

#ifdef PAGE_FAULT_DEBUG
      log_print("  Anonymous VMA:\n");
      log_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      log_print("    Overlap Size: %zu bytes\n", overlap_size);
#endif
      kmemset(dest_addr, 0, overlap_size);
    }

    // Move to the next VMA in the list
    current_vma = current_vma->next;
  }

  if (!is_any_segment_found) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "No valid VMA found for user page fault at address 0x%lx in process "
        "'%s' (PID: %zu)",
        faulting_address, current_process->name, current_process->pid);
#endif
    kfree(buffer);
    pmm_free(page_phys_addr);
    return false;
  }

  void* root_table = phys_to_virt(current_process->page_table);
  mm_result_t result =
      map_page(root_table, page_start_addr, page_phys_addr, flags);

  if (result != MM_SUCCESS) {
#ifdef PAGE_FAULT_DEBUG
    log_error(
        "Failed to map page for user page fault at address 0x%lx, Error: %d",
        faulting_address, result);
#endif
    pmm_free(page_phys_addr);
    kfree(buffer);
    return false;
  }

  kfree(buffer);
  return true;
}

bool vma_add(process_t* process, vma_t* vma) {
  vma_t* new_vma = kmalloc(sizeof(vma_t));

  if (new_vma == NULL) {
    return false;
  }

  *new_vma = *vma;

  new_vma->next = process->vma_head;
  process->vma_head = new_vma;

  return true;
}

bool vma_remove(process_t* process, vma_t* vma) {
  if (process->vma_head == NULL) {
    return false;
  }

  if (process->vma_head == vma) {
    process->vma_head = vma->next;
    kfree(vma);
    return true;
  }

  vma_t* current = process->vma_head;

  while (current->next != NULL) {
    if (current->next == vma) {
      current->next = vma->next;
      kfree(vma);
      return true;
    }
    current = current->next;
  }

  return false;
}

// void double_fault_isr_handler(struct interrupt_ecframe_s* frame) {
//   UNUSED(frame);
//   log_print("Double Fault:");
//   log_print("  RIP: 0x%lx\n", frame->rip);
//   log_print("  RSP: 0x%lx\n", frame->rsp);
//   log_print("  RFLAGS: 0x%lx\n", frame->rflags);
//   log_newline();

//   process_t* process1 = get_process_by_pid(1);
//   process_t* process2 = get_process_by_pid(2);

//   thread_t* thread1 = get_thread(process1, 1);
//   thread_t* thread2 = get_thread(process2, 4);

//   log_print("Process 1 (PID: %zu) - Thread 1 (TID: %zu):\n", process1->pid,
//             thread1->tid);

//   struct scheduler_frame_s* frame1 =
//       (struct scheduler_frame_s*)thread1->current_stack_ptr;
//   log_print("  RIP: 0x%lx\n", frame1->rip);
//   log_print("  RSP: 0x%lx\n", frame1->rsp);
//   log_print("  RFLAGS: 0x%lx\n", frame1->rflags);
//   log_print("  CS: 0x%lx\n", frame1->cs);
//   log_print("  SS: 0x%lx\n", frame1->ss);
//   log_newline();

//   struct scheduler_frame_s* frame2 =
//       (struct scheduler_frame_s*)thread2->current_stack_ptr;
//   log_print("Process 2 (PID: %zu) - Thread 2 (TID: %zu):\n", process2->pid,
//             thread2->tid);
//   log_print("  RIP: 0x%lx\n", frame2->rip);
//   log_print("  RSP: 0x%lx\n", frame2->rsp);
//   log_print("  RFLAGS: 0x%lx\n", frame2->rflags);
//   log_print("  CS: 0x%lx\n", frame2->cs);
//   log_print("  SS: 0x%lx\n", frame2->ss);
//   log_newline();

//   while (1);
// }