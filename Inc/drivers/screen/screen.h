#pragma once

#include <boot/boot.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  SCREEN_SUCCESS = 0,
  SCREEN_ERROR_INITIALIZATION_FAILED = -1,
  SCREEN_ERROR_UNSUPPORTED_BPP = -2,
  SCREEN_ERROR_OUT_OF_MEMORY = -3,
  SCREEN_ERROR_UNKNOWN = -4,
  SCREEN_INVALID_FRAMEBUFFER_ADDRESS = -5
} screen_result_t;

struct screen_state_s {
  struct FrameBuffer_s fbr;
  void (*write_pixel)(void* addr, uint32_t color);

  uint8_t* glyphs_ptr;
  uint32_t bytes_per_glyph;
  uint32_t font_width;
  uint32_t font_height;
  uint32_t bytes_per_pixel;
  uint32_t bytes_per_row;
};

screen_result_t screen_init(void);

void screen_clear(uint32_t rgb);

void screen_draw_char(char character, size_t x_position, size_t y_position,
                      uint32_t bg_rgb, uint32_t fg_rgb);

void screen_write_pixel(void* addr, uint32_t color);

uint32_t screen_convert_color(uint32_t rgb);

uint8_t* screen_get_framebuffer_addr(void);

uint32_t screen_get_screen_font_width(void);

uint32_t screen_get_screen_font_height(void);

size_t screen_get_screen_width(void);

size_t screen_get_screen_height(void);

size_t screen_get_screen_pitch(void);

size_t screen_get_screen_bpp(void);

uint8_t* screen_get_screen_glyphs_ptr(void);

uint32_t screen_get_screen_bytes_per_glyph(void);

void screen_set_screen_font_width(uint32_t width);

void screen_set_screen_font_height(uint32_t height);

void screen_set_screen_glyphs_ptr(uint8_t* ptr);

void screen_set_screen_bytes_per_glyph(size_t bytes);

void screen_draw_fill_rect(uint32_t x_start, uint32_t x_end, uint32_t y_start,
                           uint32_t y_end, uint32_t color);