#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/mmu.h>
#include <arch/x86_64/stack.h>
#include <arch/x86_64/syscall.h>
#include <fs/fs.h>
#include <kernel.h>
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

static inline bool handle_user_page_fault(uintptr_t faulting_address) {
  pflog_debug("User Page Fault Handler:\n");
  const size_t page_size = mm_get_page_size();
  process_t* current_process = scheduler_get_current_process();

  if (current_process == NULL) {
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
        pflog_error(
            "Failed to read from file for file-backed VMA at address 0x%lx",
            faulting_address);

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

  void* root_table = phys_to_virt(current_process->page_table);
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

void page_fault_isr_handler(struct interrupt_ecframe_s* frame) {
  uintptr_t faulting_address = page_fault_addr();

  pflog_debug("Page Fault Exception:\n");
  pflog_print("  RIP: 0x%lx\n", frame->rip);
  pflog_print("  Previous RSP: 0x%lx\n", frame->rsp);
  pflog_print("  RFLAGS: 0x%lx\n", frame->rflags);
  pflog_print("  Current RSP: 0x%lx\n", get_stack_top());
  pflog_print("  Address: 0x%lx\n", faulting_address);
  pflog_print("  Error code: 0x%lx\n", frame->error_code);

  if (frame->error_code & MMU_PF_PLV_MASK) {
    pflog_print("  Page-level protection violation\n");
  } else {
    pflog_print("  Non-present page\n");
  }

  if (frame->error_code & MMU_PF_WA_MASK) {
    pflog_print("  Write access\n");
  } else {
    pflog_print("  Read access\n");
  }

  if (frame->error_code & MMU_PF_UM_MASK) {
    pflog_print("  User-mode access\n");
  } else {
    pflog_print("  Kernel-mode access\n");
  }

  log_newline();

  // handle user space page fault
  if (frame->error_code & MMU_PF_UM_MASK &&
      !(frame->error_code & MMU_PF_PLV_MASK)) {
    if (faulting_address < (uintptr_t)mm_get_user_virtual_base()) {
      tty_printf(
          ANSI_COLOR_BRIGHT_RED
          "Segmentation fault: Invalid user space address 0x%lx\n" ANSI_RESET,
          faulting_address);
      log_error(
          ANSI_COLOR_BRIGHT_RED
          "Segmentation fault: Invalid user space address 0x%lx\n" ANSI_RESET,
          faulting_address);
      scheduler_terminate_current_process();

#ifdef PAGE_FAULT_DEBUG
      log_error("Segmentation fault: Invalid user space address 0x%lx",
                faulting_address);
#endif
    }

    if (handle_user_page_fault(faulting_address)) {
#ifdef PAGE_FAULT_DEBUG
      log_info("Handled user page fault at address 0x%lx", faulting_address);
#endif
      return;
    }

    log_debug(ANSI_COLOR_BRIGHT_RED
              "Failed to handle user page fault at address 0x%lx\n" ANSI_RESET,
              faulting_address);
    scheduler_terminate_current_process();

#ifdef PAGE_FAULT_DEBUG
    log_error("Failed to handle user page fault at address 0x%lx",
              faulting_address);
#endif
  }

  tty_printf("Segmentation fault at address 0x%lx, RIP: 0x%lx",
             faulting_address, frame->rip);
  log_error("Segmentation fault at address 0x%lx, RIP: 0x%lx", faulting_address,
            frame->rip);
  while (1);
}
