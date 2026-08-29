#include "elf.h"

#include <arch/x86_64/mmu.h>
#include <mm/mm.h>
#include <mm/vmm/kheap.h>
#include <process/process.h>
#include <process/scheduler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>
#include <vfs/vfs.h>

const uint8_t MAGIC_ELF_BYTES[4] = {0x7F, 'E', 'L', 'F'};

static bool is_valid_elf_hdr(const elf64_ehdr_t* ehdr) {
  for (int i = 0; i < 4; ++i) {
    if (ehdr->e_ident[i] != MAGIC_ELF_BYTES[i]) {
      return false;
    }
  }

  // #ifdef __x86_64__
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    return false;
  }

  if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
    return false;
  }

  if (ehdr->e_machine != EM_X86_64) {
    return false;
  }
  // #endif

  return true;
}

static inline bool is_valid_elf_phdr(const elf64_phdr_t* phdr) {
  if (phdr->p_memsz < phdr->p_filesz) {
    return false;
  }

  if (phdr->p_offset % phdr->p_align != phdr->p_vaddr % phdr->p_align) {
    return false;
  }

  return true;
}

static inline vma_flags_t get_elf_flag(Elf64_Word p_flags) {
  vma_flags_t flags = 0;

  if (p_flags & PF_R) {
    flags |= VMA_READ;  // Readable
  }

  if (p_flags & PF_W) {
    flags |= VMA_WRITE;  // Writable
  }

  if (p_flags & PF_X) {
    flags |= VMA_EXEC;  // Executable
  }

  return flags;
}

int load_elf_file(process_t* process, const char* path, void** entry_point) {
  vfs_t* file = kmalloc(sizeof(vfs_t));
  int ret = vfs_open(file, path, FA_READ);

  if (ret < 0) {
#ifdef ELF_LOADER_DEBUG
    log_error("Failed to open ELF file\n");
#endif
    kfree(file);
    return 1;
  }

  if (vfs_get_file_size(file) < sizeof(elf64_ehdr_t)) {
#ifdef ELF_LOADER_DEBUG
    log_error("File size is too small to be a valid ELF file\n");
#endif
    kfree(file);
    return 1;
  }

  elf64_ehdr_t* ehdr = (elf64_ehdr_t*)kmalloc(sizeof(elf64_ehdr_t));
  ret = vfs_read(file, (void*)ehdr, sizeof(elf64_ehdr_t));

  if (ret < 0) {
#ifdef ELF_LOADER_DEBUG
    log_error("Failed to read ELF header\n");
#endif
    vfs_close(file);
    kfree(ehdr);
    kfree(file);
    return 1;
  }

  if (!is_valid_elf_hdr(ehdr)) {
#ifdef ELF_LOADER_DEBUG
    log_error("Invalid ELF file\n");
#endif
    vfs_close(file);
    kfree(ehdr);
    kfree(file);
    return 1;
  }

  if (entry_point != NULL) {
    *entry_point = (void*)ehdr->e_entry;
  }

#ifdef ELF_LOADER_DEBUG
  log_print("ELF Header:\n");
  log_print("  Entry point: 0x%lx\n", ehdr->e_entry);
  log_print("  Program header offset: %lu\n", ehdr->e_phoff);
  log_print("  Size of program header: %lu\n", ehdr->e_phentsize);
  log_print("  Number of program headers: %u\n", ehdr->e_phnum);
  log_print("\n");
#endif

  elf64_phdr_t* phdr = (elf64_phdr_t*)kmalloc(sizeof(elf64_phdr_t));

  for (size_t i = 0; i < ehdr->e_phnum; ++i) {
    vfs_seek(file, ehdr->e_phoff + i * sizeof(elf64_phdr_t));
    ret = vfs_read(file, (void*)phdr, sizeof(elf64_phdr_t));

    if (ret < 0) {
#ifdef ELF_LOADER_DEBUG
      log_error("Failed to read program header\n");
#endif
      vfs_close(file);
      kfree(phdr);
      kfree(ehdr);
      kfree(file);
      return 1;
    }

    if (!(is_valid_elf_phdr(phdr) && phdr->p_type == PT_LOAD)) {
      continue;
    }
#ifdef ELF_LOADER_DEBUG
    log_print("Program Header [%d]:\n", i);
    log_print("  Type: %u\n", phdr->p_type);
    log_print("  Flags: %u\n", phdr->p_flags);
    log_print("  Offset: %lu\n", phdr->p_offset);
    log_print("  Virtual Address: 0x%lx\n", phdr->p_vaddr);
    log_print("  Physical Address: 0x%lx\n", phdr->p_paddr);
    log_print("  File Size: %lu\n", phdr->p_filesz);
    log_print("  Memory Size: %lu\n", phdr->p_memsz);
    log_print("  Alignment: %lu\n", phdr->p_align);
    log_print("\n");
#endif
    vma_flags_t flags = get_elf_flag(phdr->p_flags);

    if (phdr->p_filesz > 0) {
      // Mark the VMA as file-backed since it needs to be read from a file
      flags |= VMA_FILE_BACKED;

      vma_t vma = {.vm_start = phdr->p_vaddr,
                   .vm_end = phdr->p_vaddr + phdr->p_filesz,
                   .flags = flags,
                   .file = file,
                   .file_offset = phdr->p_offset,
                   .file_size = phdr->p_filesz,
                   .next = NULL};

      if (!vma_add(process, &vma)) {
#ifdef ELF_LOADER_DEBUG
        log_error("Failed to add VMA for ELF segment\n");
#endif
        vfs_close(file);
        kfree(phdr);
        kfree(ehdr);
        kfree(file);
        return 1;
      }
    }

    if (!(phdr->p_memsz > phdr->p_filesz)) {
#ifdef ELF_LOADER_DEBUG
      log_print("\n");
      log_print("No zeroing required for this segment.\n");
#endif
      continue;
    }

    // Clear the file-backed flag for the zeroed region
    flags &= ~VMA_FILE_BACKED;

    // Mark as anonymous since it doesn't have a file backing
    flags |= VMA_ANONYMOUS;

    vma_t vma = {.vm_start = phdr->p_vaddr + phdr->p_filesz,
                 .vm_end = phdr->p_vaddr + phdr->p_memsz,
                 .flags = flags,
                 .file = NULL,
                 .file_offset = 0,
                 .file_size = 0,
                 .next = NULL};

    if (!vma_add(process, &vma)) {
#ifdef ELF_LOADER_DEBUG
      log_error("Failed to add VMA for zeroed region of ELF segment\n");
#endif
      vfs_close(file);
      kfree(phdr);
      kfree(ehdr);
      kfree(file);
      return 1;
    }
  }

  kfree(ehdr);
  kfree(phdr);
  return 0;
}
