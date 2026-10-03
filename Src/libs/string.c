#include <libs/string.h>

void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = dest;
  const unsigned char *s = src;

  for (size_t i = 0; i < n; i++)
    d[i] = s[i];

  return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = dest;
  const unsigned char *s = src;

  if (d == s)
    return dest;

  /*
   * Non-overlapping or destination before source.
   */
  if (d < s) {
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  }
  /*
   * Destination after source.
   *
   * Copy backwards so overlapping regions are handled correctly.
   */
  else {
    for (size_t i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  }

  return dest;
}

void *memset(void *dest, int c, size_t n) {
  unsigned char *d = dest;

  for (size_t i = 0; i < n; i++)
    d[i] = (unsigned char)c;

  return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *p = a;
  const unsigned char *q = b;

  for (size_t i = 0; i < n; i++) {
    if (p[i] != q[i])
      return (int)p[i] - (int)q[i];
  }

  return 0;
}

size_t strlen(const char *str) {
  size_t len = 0;

  while (str[len] != '\0')
    len++;

  return len;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }

  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];

    if (ca != cb)
      return ca - cb;

    if (ca == '\0')
      return 0;
  }

  return 0;
}

char *strcpy(char *dest, const char *src) {
  char *original = dest;

  while ((*dest++ = *src++) != '\0')
    ;

  return original;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;

  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];

  for (; i < n; i++)
    dest[i] = '\0';

  return dest;
}

char *strchr(const char *str, int c) {
  char ch = (char)c;

  while (*str != '\0') {
    if (*str == ch)
      return (char *)str;

    str++;
  }

  if (ch == '\0')
    return (char *)str;

  return NULL;
}

char *strrchr(const char *str, int c) {
  const char *last = NULL;
  char ch = (char)c;

  while (*str != '\0') {
    if (*str == ch)
      last = str;

    str++;
  }

  if (ch == '\0')
    return (char *)str;

  return (char *)last;
}

static int is_delimiter(char c, const char *delim) {
  while (*delim != '\0') {
    if (c == *delim)
      return 1;

    delim++;
  }

  return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *token;

  /*
   * First call:
   *
   *     strtok_r(str, delim, &saveptr)
   *
   * Subsequent calls:
   *
   *     strtok_r(NULL, delim, &saveptr)
   */
  if (str == NULL) {
    if (saveptr == NULL || *saveptr == NULL)
      return NULL;

    str = *saveptr;
  }

  /*
   * Skip leading delimiters.
   *
   * This means consecutive delimiters are treated as one.
   *
   * Example:
   *
   *     "a///b"
   *
   * with '/' produces:
   *
   *     "a"
   *     "b"
   */
  while (*str != '\0' && is_delimiter(*str, delim))
    str++;

  /*
   * We reached the end of the string.
   */
  if (*str == '\0') {
    if (saveptr != NULL)
      *saveptr = str;

    return NULL;
  }

  /*
   * Beginning of the token.
   */
  token = str;

  /*
   * Find the next delimiter.
   */
  while (*str != '\0' && !is_delimiter(*str, delim))
    str++;

  /*
   * We found a delimiter.
   *
   * Replace it with '\0' so that token becomes
   * a normal C string.
   */
  if (*str != '\0') {
    *str = '\0';
    str++;
  }

  /*
   * Save position for the next call.
   */
  if (saveptr != NULL)
    *saveptr = str;

  return token;
}

char *strtok(char *str, const char *delim) {
  static char *saveptr;

  return strtok_r(str, delim, &saveptr);
}
