#pragma once

#include <mm/mm.h>
#include <scheduler/scheduler.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint8_t Elf64_UnsignedChar;

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3

#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7

typedef enum { ELFCLASSNONE = 0, ELFCLASS32 = 1, ELFCLASS64 = 2 } ELF_CLASS_t;

typedef enum { ELFDATANONE = 0, ELFDATA2LSB = 1, ELFDATA2MSB = 2 } ELF_DATA_t;

typedef enum {
  ET_NONE = 0,
  ET_REL = 1,
  ET_EXEC = 2,
  ET_DYN = 3,
  ET_CORE = 4
} ELF_TYPE_t;

typedef enum {
  EM_NONE = 0,
  EM_386 = 3,
  EM_X86_64 = 62,
  EM_AARCH64 = 183
} ELF_MACHINE_t;

typedef struct {
  Elf64_UnsignedChar e_ident[16];

  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;

  Elf64_Addr e_entry;

  Elf64_Off e_phoff;
  Elf64_Off e_shoff;

  Elf64_Word e_flags;

  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;

  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
} elf64_ehdr_t;

typedef struct {
  Elf64_Word p_type;
  Elf64_Word p_flags;
  Elf64_Off p_offset;
  Elf64_Addr p_vaddr;
  Elf64_Addr p_paddr;
  Elf64_Xword p_filesz;
  Elf64_Xword p_memsz;
  Elf64_Xword p_align;
} elf64_phdr_t;

typedef enum {
  PT_NULL = 0,
  PT_LOAD = 1,
  PT_DYNAMIC = 2,
  PT_INTERP = 3,
  PT_NOTE = 4,
  PT_SHLIB = 5,
  PT_PHDR = 6,
  PT_TLS = 7
} phdr_type_t;

typedef enum { PF_X = 1, PF_W = 2, PF_R = 4 } phdr_flags_t;

int load_elf_file(process_t* process, const char* path, void** entry_point);