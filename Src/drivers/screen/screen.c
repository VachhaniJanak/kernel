#include "screen.h"
#include "../utils/utils.h"
#include "multiboot2.h"
#include <stdbool.h>
#include <stdint.h>

struct multiboot_tag_framebuffer fbr = {0};

extern uint8_t chars[][13];

uint8_t font_width = 7;
uint8_t font_height = 12;
uint32_t screen_x = TEXT_MODE_MARGIN_X;
uint32_t screen_y = TEXT_MODE_MARGIN_Y;

bool init_screen(unsigned long mbi_addr) {
  struct multiboot_tag_framebuffer *ptr;
  if (get_mb_tag(mbi_addr, MULTIBOOT_TAG_TYPE_FRAMEBUFFER, (void **)&ptr)) {
    memcpy(&fbr, ptr, sizeof(fbr));
    if (fbr.common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
      return false;
    }
    return true;
  }
  return false;
}

static inline uint32_t mk_color(uint32_t rgb) {

  uint8_t r = (uint8_t)(rgb >> 16);
  uint8_t g = (uint8_t)(rgb >> 8);
  uint8_t b = (uint8_t)(rgb >> 0);

  uint8_t rcolor = (r >> (8 - fbr.framebuffer_red_mask_size));
  uint8_t gcolor = (g >> (8 - fbr.framebuffer_green_mask_size));
  uint8_t bcolor = (b >> (8 - fbr.framebuffer_blue_mask_size));

  uint32_t color = (rcolor << fbr.framebuffer_red_field_position) |
                   (gcolor << fbr.framebuffer_green_field_position) |
                   (bcolor << fbr.framebuffer_blue_field_position);
  return color;
}

static inline void _wpixel(void *addr, uint32_t color) {
  switch (fbr.common.framebuffer_bpp) {

  case 32:
    *(uint32_t *)addr = color;
    return;
  case 24:
    uint32_t *pixel = (uint32_t *)addr;
    *pixel = (color & 0xffffff) | (*pixel & 0xff000000);
    return;
  case 16:
    *(uint16_t *)addr = (uint16_t)color;
    return;
  case 8:
    *(uint8_t *)addr = (uint8_t)color;
    return;

  default:;
  }
}

void clr_screen(uint32_t rgb) {
  uint32_t color = mk_color(rgb);

  void *addr = (void *)fbr.common.framebuffer_addr;
  uint32_t bypp = fbr.common.framebuffer_bpp / 8;
  serial_printf("Frame buffer Addr 0x%x\n", addr);
  serial_printf("Size of buffer : %d %d %d %d\n", fbr.common.framebuffer_height,
                fbr.common.framebuffer_width, fbr.common.framebuffer_pitch,
                fbr.common.framebuffer_bpp);
  for (uint32_t y = 0; y < fbr.common.framebuffer_height; y++) {
    for (uint32_t x = 0; x < fbr.common.framebuffer_width; x++) {
      _wpixel(addr + y * fbr.common.framebuffer_pitch + x * bypp, color);
    }
  }
}

void draw_char(DrawCharTypedef ch_info) {
  void *addr = (void *)fbr.common.framebuffer_addr;
  uint32_t bypp = fbr.common.framebuffer_bpp / 8;

  uint32_t bg_color = mk_color(ch_info.bg_rgb);
  uint32_t fg_color = mk_color(ch_info.fg_rgb);

  int ch_idx = ch_info.ch - 32;
  uint32_t y = ch_info.y_pos;
  uint32_t x = ch_info.x_pos;

  for (int j = font_height; j > -1; j--, y++) {
    x = ch_info.x_pos;
    for (int i = font_width; i > -1; i--, x++) {
      if (chars[ch_idx][j] & (1 << i)) {
        _wpixel(addr + y * fbr.common.framebuffer_pitch + x * bypp, fg_color);
      } else {
        _wpixel(addr + y * fbr.common.framebuffer_pitch + x * bypp, bg_color);
      }
    }
  }
}

static inline void _scrl() {
  void *addr = (void *)fbr.common.framebuffer_addr;
  const uint32_t gap = font_height + SPACE_BTW_LINE;
  for (uint32_t y = gap + TEXT_MODE_MARGIN_Y; y < fbr.common.framebuffer_height;
       y += gap) {
    memcpy(addr + (y - gap) * fbr.common.framebuffer_pitch,
addr + y * fbr.common.framebuffer_pitch,
           fbr.common.framebuffer_pitch * gap);
  }
}

void _putchar(char character) {

  if (character == '\n') {
    screen_y += font_height + SPACE_BTW_LINE;
    screen_x = TEXT_MODE_MARGIN_X;
    return;
  }

  if (screen_x + font_width + TEXT_MODE_MARGIN_X >=
      fbr.common.framebuffer_width) {
    screen_y += font_height + SPACE_BTW_LINE;
    screen_x = TEXT_MODE_MARGIN_X;
  }

  if (screen_y + font_height + TEXT_MODE_MARGIN_Y >=
      fbr.common.framebuffer_height) {
    _scrl();
    screen_y -= (font_height + TEXT_MODE_MARGIN_Y);
    screen_x = TEXT_MODE_MARGIN_X;
  }

  DrawCharTypedef ch_info = {.x_pos = screen_x,
                             .y_pos = screen_y,
                             .fg_rgb = 0xffffffff,
                             .bg_rgb = 0xff000000,
                             .ch = character};

  draw_char(ch_info);
  screen_x += font_width + SPACE_BTW_CHAR;
}
