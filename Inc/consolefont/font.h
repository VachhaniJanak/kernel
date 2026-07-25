#pragma once

#include <stdint.h>
#include <stddef.h>

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

void init_psf_font(void);

uint8_t* get_psf_glyphs(char glyph);

size_t get_psf_glyph_width(void);

size_t get_psf_glyph_height(void);
