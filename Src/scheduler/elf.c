#include "elf.h"

#include <arch/x86_64/mmu.h>
#include <mm/mm.h>
#include <mm/vmm/kheap.h>
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

#ifdef __x86_64__
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    return false;
  }

  if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
    return false;
  }

  if (ehdr->e_machine != EM_X86_64) {
    return false;
  }
#endif

  return true;
}

static bool is_valid_phdr(const elf64_phdr_t* phdr) {
  if (phdr->p_type != PT_LOAD) {
    return false;
  }

  if (phdr->p_memsz < phdr->p_filesz) {
    return false;
  }

  if (phdr->p_offset % phdr->p_align != phdr->p_vaddr % phdr->p_align) {
    return false;
  }

  return true;
}

static uint64_t gets_flag(Elf64_Word p_flags) {
  uint64_t flags = 0;

  if (p_flags & PF_R) {
    flags |= MMU_PRESENT;  // Readable
  }

  if (p_flags & PF_W) {
    flags |= MMU_WRITABLE;  // Writable
  }

  if (!(p_flags & PF_X)) {
    flags |= MMU_NX;  // Not Executable
  }

  return flags;
}

int load_elf_file(const char* path, void** entry_point) {
  vfs_t file;

  int ret = vfs_open(&file, path, FA_READ);

  if (ret < 0) {
    log_error("Failed to open ELF file\n");
    return 1;
  }

  if (vfs_get_file_size(&file) < sizeof(elf64_ehdr_t)) {
    log_error("File size is too small to be a valid ELF file\n");
    return 1;
  }

  const size_t max_buffer_size = 4096;
  uint8_t* buffer = (uint8_t*)kmalloc(max_buffer_size);
  elf64_ehdr_t ehdr;

  vfs_read(&file, buffer, sizeof(elf64_ehdr_t));
  kmemcpy(&ehdr, buffer, sizeof(elf64_ehdr_t));

  if (!is_valid_elf_hdr(&ehdr)) {
    log_error("Invalid ELF file\n");
    vfs_close(&file);
    kfree(buffer);
    return 1;
  }

  if (entry_point != NULL) {
    *entry_point = (void*)ehdr.e_entry;
  }

#ifdef SCHEDULER_DEBUG
  log_print("ELF Header:\n");
  log_print("  Entry point: 0x%lx\n", ehdr.e_entry);
  log_print("  Program header offset: %lu\n", ehdr.e_phoff);
  log_print("  Size of program header: %lu\n", ehdr.e_phentsize);
  log_print("  Number of program headers: %u\n", ehdr.e_phnum);
#endif

  elf64_phdr_t phdr;

  for (size_t i = 0; i < ehdr.e_phnum; ++i) {
    vfs_seek(&file, ehdr.e_phoff + i * sizeof(elf64_phdr_t));
    vfs_read(&file, buffer, sizeof(elf64_phdr_t));
    kmemcpy(&phdr, buffer, sizeof(elf64_phdr_t));

    if (!is_valid_phdr(&phdr)) {
      continue;
    }

#ifdef SCHEDULER_DEBUG
    log_print("Program Header [%d]:\n", i);
    log_print("  Type: %u\n", phdr.p_type);
    log_print("  Flags: %u\n", phdr.p_flags);
    log_print("  Offset: %lu\n", phdr.p_offset);
    log_print("  Virtual Address: 0x%lx\n", phdr.p_vaddr);
    log_print("  Physical Address: 0x%lx\n", phdr.p_paddr);
    log_print("  File Size: %lu\n", phdr.p_filesz);
    log_print("  Memory Size: %lu\n", phdr.p_memsz);
    log_print("  Alignment: %lu\n", phdr.p_align);
#endif

    // allocate memory
    uint64_t flags = gets_flag(phdr.p_flags);
    size_t vaddr_end = phdr.p_vaddr + phdr.p_memsz;

    void* start_addr = PAGE_ALIGN_DOWN((void*)phdr.p_vaddr, 0x1000);
    void* end_addr = PAGE_ALIGN_UP((void*)vaddr_end, 0x1000);

    bool res = allocate_userspace(start_addr, phdr.p_memsz,
                                  MMU_PRESENT | MMU_WRITABLE | MMU_USER_MEMORY);

    if (!res) {
      log_error("Failed to allocate userspace memory for segment %d\n", i);
      vfs_close(&file);
      kfree(buffer);
      return 1;
    }

    size_t offset = phdr.p_offset;
    size_t bytes_read = 0;

    while (bytes_read < phdr.p_filesz) {
      size_t read_size = max_buffer_size;

      if (read_size > phdr.p_filesz - bytes_read) {
        read_size = phdr.p_filesz - bytes_read;
      }

      vfs_seek(&file, offset);
      vfs_read(&file, buffer, read_size);

      void* src = (void*)(phdr.p_vaddr + bytes_read);
      kmemcpy(src, buffer, read_size);

      bytes_read += read_size;
      offset += read_size;

      log_info("Copied %zu bytes to virtual address 0x%lx", read_size,
               (uintptr_t)src);
    }

    if (!(phdr.p_memsz > phdr.p_filesz)) {
      continue;
    }

    size_t bss_size = phdr.p_memsz - phdr.p_filesz;
    void* bss_start = (void*)(phdr.p_vaddr + phdr.p_filesz);
    kmemset(bss_start, 0, bss_size);
  }

  kfree(buffer);
  vfs_close(&file);
  return 0;
}
