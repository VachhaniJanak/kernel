#include <stdint.h>
#include <utils/utils.h>

void* kmemcpy(void* restrict dest, const void* restrict src, size_t n) {
  uint8_t* restrict pdest = dest;
  const uint8_t* restrict psrc = src;

  for (size_t i = 0; i < n; i++) pdest[i] = psrc[i];

  return dest;
}

void* kmemset(void* s, int c, size_t n) {
  uint8_t* p = s;

  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;

  return s;
}

void* kmemmove(void* dest, const void* src, size_t n) {
  uint8_t* pdest = dest;
  const uint8_t* psrc = src;

  if ((uintptr_t)src > (uintptr_t)dest)
    for (size_t i = 0; i < n; i++) pdest[i] = psrc[i];

  else if ((uintptr_t)src < (uintptr_t)dest)
    for (size_t i = n; i > 0; i--) pdest[i - 1] = psrc[i - 1];

  return dest;
}

int kmemcmp(const void* s1, const void* s2, size_t n) {
  const uint8_t* p1 = s1;
  const uint8_t* p2 = s2;

  for (size_t i = 0; i < n; i++)
    if (p1[i] != p2[i]) return p1[i] < p2[i] ? -1 : 1;

  return 0;
}

char *kstrchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }

    if (c == '\0')
        return (char *)s;

    return NULL;
}

void kstrcpy(char* dest, const char* src) {
  while (*src) {
    *dest++ = *src++;
  }
  *dest = '\0';
}

