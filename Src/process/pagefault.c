#include "pagefault.h"

#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/syscall.h>
#include <fs/fs.h>
#include <kernel.h>
#include <libs/string.h>
#include <mm/kheap/kheap.h>
#include <mm/mm.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <mm/vmm/vmm.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <process/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tty/tty_output.h>
#include <utils/log.h>
#include <utils/utils.h>

#include "elf.h"
#include "vma.h"

// #define PAGE_FAULT_DEBUG

#ifdef PAGE_FAULT_DEBUG
#define pflog_print(fmt, ...) (log_print(fmt, ##__VA_ARGS__))
#define pflog_error(fmt, ...) (log_error(fmt, ##__VA_ARGS__))
#define pflog_debug(fmt, ...) (log_debug(fmt, ##__VA_ARGS__))
#else
#define pflog_print(fmt, ...) ((void)0)
#define pflog_error(fmt, ...) ((void)0)
#define pflog_debug(fmt, ...) ((void)0)
#endif

extern struct mm_state_s mm_state;

static void page_fault_debug_print(struct interrupt_ecframe_s* frame,
                                   uintptr_t faulting_address);

// translate the error code into
static inline uint64_t translate_page_fault_error(
    struct interrupt_ecframe_s* frame) {
  uint64_t error_code = 0;

  if (frame->error_code & MMU_PF_PLV_MASK) error_code |= PF_ERR_PRESENT;
  if (frame->error_code & MMU_PF_WA_MASK) error_code |= PF_ERR_WRITE;
  if (frame->error_code & MMU_PF_UM_MASK) error_code |= PF_ERR_USER;
  if (frame->error_code & MMU_PF_RSVD_MASK) error_code |= PF_ERR_RSVD;
  if (frame->error_code & MMU_PF_IFETCH_MASK) error_code |= PF_ERR_IFETCH;

  return error_code;
}

static inline bool handle_user_page_not_present(uintptr_t faulting_address) {
  pflog_debug("User Page Fault Handler:\n");
  const size_t page_size = mm_get_page_size();
  thread_t* current_thread = scheduler_get_current_thread();

  if (!current_thread) {
    pflog_error("  No current thread found while handling page fault");
    return false;
  }

  process_t* current_process = current_thread->parent_process;

  if (!current_process) {
    pflog_error("  No current process found while handling page fault");
    return false;
  }

  pflog_print("  Current process: '%s' (PID: %zu)\n", current_process->name,
              current_process->pid);
  pflog_print("  Current thread: '%s' (TID: %zu)\n",
              scheduler_get_current_thread()->name,
              scheduler_get_current_thread()->tid);

  void* page_start_addr = (void*)page_align_down(faulting_address, page_size);
  void* page_phys_addr = pmm_alloc(&mm_state.pmm_state, page_size);

  if (page_phys_addr == NULL) {
    pflog_error(
        "Failed to allocate physical page for user page fault at address "
        "0x%lx",
        faulting_address);
    return false;
  }

  void* buffer = kmalloc(page_size);

  if (buffer == NULL) {
    pflog_error(
        "Failed to allocate buffer for user page fault at address 0x%lx",
        faulting_address);
    pmm_free(&mm_state.pmm_state, page_phys_addr);
    return false;
  }

  void* page_virt_addr = phys_to_virt(page_phys_addr);

  // Check if the faulting address falls within any of the process's VMAs
  vma_t* current_vma = current_process->vma_head;
  mm_flags_t flags = MM_FLAG_USER;
  bool is_any_segment_found = false;

  pflog_print("  Allocating virtual page at address 0x%lx\n",
              (uintptr_t)page_start_addr);

  while (current_vma != NULL) {
    uintptr_t start, end;

    // Check if the faulting address falls within the current VMA
    if (!vma_get_intersection(current_vma, (uintptr_t)page_start_addr,
                              (uintptr_t)page_start_addr + page_size, &start,
                              &end)) {
      current_vma = current_vma->next;
      continue;
    }

    is_any_segment_found = true;
    size_t overlap_size = end - start;
    flags |= vma_get_flags(current_vma->flags);

    pflog_print("  Found overlapping VMA:\n");
    pflog_print("    VMA Start: 0x%lx\n", current_vma->vm_start);
    pflog_print("    VMA End: 0x%lx\n", current_vma->vm_end);
    pflog_print("    Overlap Start: 0x%lx\n", start);
    pflog_print("    Overlap End: 0x%lx\n", end);
    pflog_print("    Overlap Size: %zu bytes\n", overlap_size);
    pflog_print("    VMA Flags: 0x%x\n", current_vma->flags);

    if (current_vma->flags & VMA_FILE_BACKED) {
      size_t file_offset =
          current_vma->file_offset + (start - current_vma->vm_start);

      fs_lseek(current_vma->file, file_offset, VFS_SEEK_SET);
      int ret = fs_read(current_vma->file, buffer, overlap_size);

      if (ret < 0) {
        log_error(
            "Failed to read from file for file-backed VMA at address 0x%lx, Error: %d",
            faulting_address, ret);

        kfree(buffer);
        pmm_free(&mm_state.pmm_state, page_phys_addr);
        return false;
      }

      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

      pflog_print("  File-Backed VMA:\n", ret);
      pflog_print("    File Offset: 0x%lx\n", file_offset);
      pflog_print("    Bytes Read: %d\n", ret);
      pflog_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      pflog_print("    Overlap Size: %zu bytes\n", overlap_size);
      kmemcpy(dest_addr, buffer, overlap_size);

    } else if (current_vma->flags & VMA_ANONYMOUS) {
      if (current_vma->flags & VMA_NONE) {
        // pages not accessible, return false
        return false;
      }

      uintptr_t addr_offset = start - (uintptr_t)page_start_addr;
      void* dest_addr = (void*)((uintptr_t)page_virt_addr + addr_offset);

      pflog_print("  Anonymous VMA:\n");
      pflog_print("    Destination Address: 0x%lx\n", (uintptr_t)dest_addr);
      pflog_print("    Overlap Size: %zu bytes\n", overlap_size);
      kmemset(dest_addr, 0, overlap_size);
    }

    // Move to the next VMA in the list
    current_vma = current_vma->next;
  }

  if (!is_any_segment_found) {
    pflog_error(
        "No valid VMA found for user page fault at address 0x%lx in process "
        "'%s' (PID: %zu)",
        faulting_address, current_process->name, current_process->pid);
    kfree(buffer);
    pmm_free(&mm_state.pmm_state, page_phys_addr);
    return false;
  }

  void* root_table = phys_to_virt((void*)current_process->page_table);
  mm_result_t result =
      map_page(root_table, page_start_addr, page_phys_addr, flags);

  if (result != MM_SUCCESS) {
    pflog_error(
        "Failed to map page for user page fault at address 0x%lx, Error: %d",
        faulting_address, result);
    pmm_free(&mm_state.pmm_state, page_phys_addr);
    kfree(buffer);
    return false;
  }

  kfree(buffer);
  return true;
}

/*
  Handle user page-level protection violation.
*/
static inline bool handle_user_pl_pv(uintptr_t faulting_address,
                                     uint64_t error_code) {
  process_t* current_process = scheduler_get_current_process();
  // faulting_address = page_align_down(faulting_address, mm_state.page_size);

  uintptr_t root_table =
      (uintptr_t)phys_to_virt((void*)current_process->page_table);

  if (error_code & PF_ERR_WRITE) {
    uintptr_t pte = 0;
    mm_result_t result =
        get_pte_for_address((void*)root_table, (void*)faulting_address, &pte);

    const uint64_t phys_mask = mm_get_phys_mask();
    mm_flags_t mm_flags = mm_get_mm_flags(pte);
    uintptr_t phys_addr = pte & phys_mask;

    if (result != MM_SUCCESS || phys_addr == 0) {
      return false;
    }

    if (!(mm_flags & MM_FLAG_COW)) {
      return false;
    }

    frame_info_t* frame_info =
        pmm_get_frame_info(&mm_state.pmm_state, (void*)(phys_addr));

    mm_flags |= MM_FLAG_WRITABLE;
    mm_flags &= ~MM_FLAG_COW;  // Clear the COW flag

    if (frame_info->refcount == 1) {
      // Only one reference, just update the PTE to be writable
      mm_result_t map_result = change_page_flags(
          (void*)root_table, (void*)faulting_address, mm_flags);

      if (map_result != MM_SUCCESS) {
        return false;
      }

    } else {
      // More than one reference, need to copy the page
      void* new_phys_page = pmm_alloc(&mm_state.pmm_state, mm_state.page_size);

      if (!new_phys_page) {
        return false;
      }

      void* old_virt_page = phys_to_virt((void*)(phys_addr));
      void* new_virt_page = phys_to_virt(new_phys_page);

      memcpy(new_virt_page, old_virt_page, mm_state.page_size);

      // Update the PTE to point to the new page and make it writable
      uintptr_t existing_page;
      mm_result_t map_result =
          remap_page((void*)root_table, (void*)faulting_address, new_phys_page,
                     &existing_page, mm_flags);

      if (map_result != MM_SUCCESS) {
        pmm_free(&mm_state.pmm_state, new_phys_page);
        return false;
      }

      // Decrement the refcount of the old page
      pmm_decrement_page_refcount(&mm_state.pmm_state, (void*)(phys_addr));
    }

    return true;
  }

  return false;
}

void user_page_fault_handler(struct interrupt_ecframe_s* frame,
                             uintptr_t faulting_address, uint64_t error_code) {
  UNUSED(frame);

  pflog_debug("User Page Fault Handler:\n");
  thread_t* current_thread = scheduler_get_current_thread();
  process_t* current_process = current_thread->parent_process;

  pflog_print("  Current process: '%s' (PID: %zu)\n", current_process->name,
              current_process->pid);
  pflog_print("  Current thread: '%s' (TID: %zu)\n", current_thread->name,
              current_thread->tid);

  // check if the faulting address is below the user space base address
  if (faulting_address < mm_get_user_virtual_base()) {
    tty_printf("Segmentation fault at address 0x%lx\n", faulting_address);
    terminate_current_process();
    log_info("Segmentation fault at address 0x%lx in process '%s' (PID: %zu)",
             faulting_address, current_process->name, current_process->pid);
    return;
  }

  // handle user space page-level protection violation
  else if (error_code & PF_ERR_PRESENT) {
    if (handle_user_pl_pv(faulting_address, error_code)) {
      return;
    }
  }

  // handle user space page not present fault
  else {
    if (handle_user_page_not_present(faulting_address)) {
      return;
    }
  }

  tty_printf("Segmentation fault at address 0x%lx\n", faulting_address);
  terminate_current_process();
  log_info("Segmentation fault at address 0x%lx in process '%s' (PID: %zu)",
             faulting_address, current_process->name, current_process->pid);
  return;
}

void page_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  uintptr_t faulting_address = page_fault_addr();
  uint64_t error_code = translate_page_fault_error(frame);
  thread_t* current_thread = scheduler_get_current_thread();

#ifdef PAGE_FAULT_DEBUG
  page_fault_debug_print(frame, faulting_address);
#endif

  // handle user space page fault
  if (error_code & PF_ERR_USER || current_thread->status == TASK_SYSCALL_WAIT) {
    user_page_fault_handler(frame, faulting_address, error_code);
    return;
  }

  // handle kernel space page fault
  log_error("kernel page fault at address 0x%lx, RIP: 0x%lx", faulting_address,
            frame->rip);
  while (1);
}

static void page_fault_debug_print(struct interrupt_ecframe_s* frame,
                                   uintptr_t faulting_address) {
  log_debug("Page Fault Exception:\n");
  log_print("  RIP: 0x%lx\n", frame->rip);
  log_print("  Previous RSP: 0x%lx\n", frame->rsp);
  log_print("  RFLAGS: 0x%lx\n", frame->rflags);
  log_print("  Current RSP: 0x%lx\n", get_stack_top());
  log_print("  Address: 0x%lx\n", faulting_address);
  log_print("  Error code: 0x%lx\n", frame->error_code);
  log_print("  CS: 0x%lx\n", frame->cs);
  log_print("  SS: 0x%lx\n", frame->ss);

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

  if (frame->error_code & MMU_PF_RSVD_MASK) {
    log_print("  Reserved bit violation\n");
  } else {
    log_print("  No reserved bit violation\n");
  }

  if (frame->error_code & MMU_PF_IFETCH_MASK) {
    log_print("  Instruction fetch\n");
  } else {
    log_print("  Data access\n");
  }

  log_newline();
}
