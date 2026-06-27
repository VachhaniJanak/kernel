#pragma once

#include "limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMMAP_USABLE LIMINE_MEMMAP_USABLE
#define MEMMAP_RESERVED LIMINE_MEMMAP_RESERVED
#define MEMMAP_ACPI_RECLAIMABLE LIMINE_MEMMAP_ACPI_RECLAIMABLE
#define MEMMAP_ACPI_NVS LIMINE_MEMMAP_ACPI_NVS
#define MEMMAP_BAD_MEMORY LIMINE_MEMMAP_BAD_MEMORY
#define MEMMAP_BOOTLOADER_RECLAIMABLE LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE
#define MEMMAP_EXECUTABLE_AND_MODULES LIMINE_MEMMAP_EXECUTABLE_AND_MODULES
#define MEMMAP_FRAMEBUFFER LIMINE_MEMMAP_FRAMEBUFFER
#define MEMMAP_RESERVED_MAPPED LIMINE_MEMMAP_RESERVED_MAPPED

struct FrameBuffer_s {
  void *address;
  uint64_t width;
  uint64_t height;
  uint64_t pitch;
  uint16_t bpp;
  uint8_t red_mask_size;
  uint8_t red_mask_shift;
  uint8_t green_mask_size;
  uint8_t green_mask_shift;
  uint8_t blue_mask_size;
  uint8_t blue_mask_shift;
};

struct MemoryMapEntry_s {
  uint64_t base;
  uint64_t length;
  uint64_t type;
};

bool isBootOk(void);

void getFramebufferAddr(struct FrameBuffer_s *framebuffer);

size_t getMMapEntryCount(void);

bool copyMMapEntry(struct MemoryMapEntry_s *dest);

uintptr_t getHHDMOffset(void);

void *getRSDT(void);