#include "mmutils.h"
#include <boot/boot.h>
#include <stddef.h>
#include <stdint.h>

void get_max_len(struct MemoryMapEntry_s *entries, size_t noEntries,
                 uint8_t type, size_t *max_usable_length,
                 uintptr_t *max_usable_base) {

  *max_usable_length = 0;
  *max_usable_base = 0;

  for (size_t i = 0; i < noEntries; i++) {

    if (type != entries[i].type)
      continue;

    if (entries[i].length > *max_usable_length) {
      *max_usable_length = entries[i].length;
      *max_usable_base = entries[i].base;
    }
  }
}
