#include <consolefont/font.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>

#define PSF1_MAGIC 0x0436
#define PSF2_MAGIC 0x864AB572

// #define CONSOLE_FONT_DEBUG

extern uint8_t _psf_font_start[];
extern uint8_t _psf_font_end[];
extern uint8_t _psf_font_size[];

static psf2_header_t* psf2_header = NULL;
static uint8_t* psf_glyphs = NULL;

void consolefont_init(void) {
  psf1_header_t* psf1_header = (psf1_header_t*)_psf_font_start;
  psf2_header = (psf2_header_t*)_psf_font_start;

  if (psf1_header->magic == PSF1_MAGIC) {
    return;
  }

  if (psf2_header->magic == PSF2_MAGIC) {
#ifdef CONSOLE_FONT_DEBUG
    log_print(
        "PSF2 Font detected: %u glyphs, %u bytes per glyph, %ux%u pixels\n",
        psf2_header->numglyph, psf2_header->bytesperglyph, psf2_header->width,
        psf2_header->height);

#endif

    psf_glyphs = _psf_font_start + psf2_header->headersize;
  }
}

uint8_t* consolefont_get_glyphs(char glyph) {
  return psf_glyphs + (glyph * psf2_header->bytesperglyph);
}

uint8_t* consolefont_get_glyphs_ptr(void) { return psf_glyphs; }

size_t consolefont_get_bytes_per_glyph(void) {
  return psf2_header->bytesperglyph;
}

size_t consolefont_get_glyph_width(void) { return psf2_header->width; }

size_t consolefont_get_glyph_height(void) { return psf2_header->height; }
