#include <stdint.h>
#include <utils/utils.h>

void *kmemcpy(void *restrict dest, const void *restrict src, size_t n) {
  uint8_t *restrict pdest = dest;
  const uint8_t *restrict psrc = src;

  for (size_t i = 0; i < n; i++)
    pdest[i] = psrc[i];

  return dest;
}

void *kmemset(void *s, int c, size_t n) {
  uint8_t *p = s;

  for (size_t i = 0; i < n; i++)
    p[i] = (uint8_t)c;

  return s;
}
