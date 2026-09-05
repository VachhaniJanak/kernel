#include <boot/boot.h>
#include <drivers/screen/screen.h>
#include <platform/attributes.h>
#include <stdint.h>
#include <utils/log.h>

// #define SCREEN_DEBUG

static struct screen_state_s screen = {0};

static inline void w32bitpx(void* addr, uint32_t color) {
  *(uint32_t*)addr = color;
}

static inline void w24bitpx(void* addr, uint32_t color) {
  uint32_t* pixel = (uint32_t*)addr;
  *pixel = (color & 0xffffff) | (*pixel & 0xff000000);
}

static inline void w16bitpx(void* addr, uint32_t color) {
  *(uint16_t*)addr = (uint16_t)color;
}

static inline void w8bitpx(void* addr, uint32_t color) {
  *(uint8_t*)addr = (uint8_t)color;
}

screen_result_t screen_init(void) {
  getFramebufferAddr(&screen.fbr);

  if (screen.fbr.address == NULL) {
    return SCREEN_INVALID_FRAMEBUFFER_ADDRESS;
  }

  screen.bytes_per_pixel = screen.fbr.bpp / 8;

#ifdef SCREEN_DEBUG
  log_print("Screen initialized with the following parameters:\n");
  log_print("  Framebuffer address: %p\n", screen.fbr.address);
  log_print("  Framebuffer width: %lu\n", screen.fbr.width);
  log_print("  Framebuffer height: %lu\n", screen.fbr.height);
  log_print("  Framebuffer pitch: %lu\n", screen.fbr.pitch);
  log_print("  Framebuffer bpp: %u\n", screen.fbr.bpp);
  log_print("  Font width: %u\n", screen.font_width);
  log_print("  Font height: %u\n", screen.font_height);
  log_print("  Bytes per glyph: %u\n", screen.bytes_per_glyph);
  log_print("  Bytes per pixel: %u\n", screen.bytes_per_pixel);
  log_print("  Bytes per row: %u\n", screen.bytes_per_row);
#endif

  switch (screen.fbr.bpp) {
    case 32:
      screen.write_pixel = &w32bitpx;
      return SCREEN_SUCCESS;
    case 24:
      screen.write_pixel = &w24bitpx;
      return SCREEN_SUCCESS;
    case 16:
      screen.write_pixel = &w16bitpx;
      return SCREEN_SUCCESS;
    case 8:
      screen.write_pixel = &w8bitpx;
      return SCREEN_SUCCESS;
    default:
      screen.write_pixel = NULL;
      return SCREEN_ERROR_UNSUPPORTED_BPP;
  }
}

static inline uint32_t convert_color(uint32_t rgb) {
  uint8_t r = (uint8_t)(rgb >> 16);
  uint8_t g = (uint8_t)(rgb >> 8);
  uint8_t b = (uint8_t)(rgb >> 0);

  uint8_t rcolor = (r >> (8 - screen.fbr.red_mask_size));
  uint8_t gcolor = (g >> (8 - screen.fbr.green_mask_size));
  uint8_t bcolor = (b >> (8 - screen.fbr.blue_mask_size));

  uint32_t color = (rcolor << screen.fbr.red_mask_shift) |
                   (gcolor << screen.fbr.green_mask_shift) |
                   (bcolor << screen.fbr.blue_mask_shift);
  return color;
}

uint32_t screen_convert_color(uint32_t rgb) { return convert_color(rgb); }

static inline void clear(uint32_t rgb, void (*write_pixel)(void*, uint32_t)) {
  uint32_t color = convert_color(rgb);
  uint8_t* addr = (uint8_t*)screen.fbr.address;
  uint32_t bypp = screen.bytes_per_pixel;

  for (size_t y = 0; y < screen.fbr.height; ++y) {
    for (size_t x = 0; x < screen.fbr.width; ++x) {
      write_pixel(addr + y * screen.fbr.pitch + x * bypp, color);
    }
  }
}

void screen_clear(uint32_t rgb) { clear(rgb, screen.write_pixel); }

static inline uint8_t* get_glyphs(char glyph) {
  return screen.glyphs_ptr + (glyph * screen.bytes_per_glyph);
}

void draw_char(char character, size_t x_position, size_t y_position,
               uint32_t bg_rgb, uint32_t fg_rgb,
               void (*write_pixel)(void*, uint32_t)) {
  uint8_t* addr = (uint8_t*)screen.fbr.address;
  uint32_t bypp = screen.bytes_per_pixel;
  uint32_t bytes_per_row = screen.bytes_per_row;

  uint32_t bg_color = convert_color(bg_rgb);
  uint32_t fg_color = convert_color(fg_rgb);

  uint32_t y = y_position;
  uint32_t x = x_position;

  for (size_t row = 0; row < screen.font_height; row++, y++) {
    x = x_position;

    for (size_t byte = 0; byte < bytes_per_row; byte++) {
      size_t idx = row * bytes_per_row + byte;
      uint8_t bits = get_glyphs(character)[idx];

      for (size_t bit = 0; bit < 8; bit++, x++) {
        if (bits & (0x80 >> bit)) {
          write_pixel(addr + y * screen.fbr.pitch + x * bypp, fg_color);
        } else {
          write_pixel(addr + y * screen.fbr.pitch + x * bypp, bg_color);
        }
      }
    }
  }
}

void screen_draw_char(char character, size_t x_position, size_t y_position,
                      uint32_t bg_rgb, uint32_t fg_rgb) {
  draw_char(character, x_position, y_position, bg_rgb, fg_rgb,
            screen.write_pixel);
}

void screen_write_pixel(void* addr, uint32_t color) {
  screen.write_pixel(addr, color);
}

uint8_t* screen_get_framebuffer_addr(void) { return screen.fbr.address; }

uint32_t screen_get_screen_font_width(void) { return screen.font_width; }

uint32_t screen_get_screen_font_height(void) { return screen.font_height; }

size_t screen_get_screen_width(void) { return screen.fbr.width; }

size_t screen_get_screen_height(void) { return screen.fbr.height; }

size_t screen_get_screen_pitch(void) { return screen.fbr.pitch; }

size_t screen_get_screen_bpp(void) { return screen.fbr.bpp; }

uint8_t* screen_get_screen_glyphs_ptr(void) { return screen.glyphs_ptr; }

uint32_t screen_get_screen_bytes_per_glyph(void) {
  return screen.bytes_per_glyph;
}

void screen_set_screen_font_width(uint32_t width) {
  screen.font_width = width;
  screen.bytes_per_row = (screen.font_width + 7) / 8;
}

void screen_set_screen_font_height(uint32_t height) {
  screen.font_height = height;
}

void screen_set_screen_glyphs_ptr(uint8_t* ptr) { screen.glyphs_ptr = ptr; }

void screen_set_screen_bytes_per_glyph(size_t bytes) {
  screen.bytes_per_glyph = bytes;
}

void screen_draw_fill_rect(uint32_t x_start, uint32_t x_end, uint32_t y_start,
                           uint32_t y_end, uint32_t color) {
  uint8_t* fbr = (uint8_t*)screen.fbr.address;
  uint32_t pitch = screen.fbr.pitch;
  uint32_t bypp = screen.bytes_per_pixel;
  uint32_t color_converted = convert_color(color);

  for (uint32_t y = y_start; y < y_end; y++) {
    for (uint32_t x = x_start; x < x_end; x++) {
      screen.write_pixel(fbr + y * pitch + x * bypp, color_converted);
    }
  }
}