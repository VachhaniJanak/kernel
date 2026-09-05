#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t magic;
  uint8_t mode;
  uint8_t charsize;
} psf1_header_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t headersize;
  uint32_t flags;
  uint32_t numglyph;
  uint32_t bytesperglyph;
  uint32_t height;
  uint32_t width;
} psf2_header_t;

void consolefont_init(void);

uint8_t* consolefont_get_glyphs(char glyph);

uint8_t* consolefont_get_glyphs_ptr(void);

size_t consolefont_get_bytes_per_glyph(void);

size_t consolefont_get_glyph_width(void);

size_t consolefont_get_glyph_height(void);
