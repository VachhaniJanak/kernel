#include <utils/utils.h>
#include <boot/multiboot2.h>
#include <stdbool.h>
#include <stdint.h>

bool get_mb_tag(unsigned long addr, uint8_t tag_type, void **ptr) {
  struct multiboot_tag *tag;
  for (tag = (struct multiboot_tag *)(addr + 8);
       tag->type != MULTIBOOT_TAG_TYPE_END;
       tag =
           (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {

    if (tag->type == tag_type) {
      *ptr = (uint8_t *)tag;
      return true;
    }
  }
  return false;
}

void memcpy(void *restrict dest, const void *restrict src, size_t n) {
  uint8_t *d = dest;
  const uint8_t *s = src;

  while (n--)
    *d++ = *s++;
}

void memset(void *ptr, uint8_t c, size_t n) {
  uint8_t *p = ptr;
  while (n--) {
    *p++ = c;
  }
}
