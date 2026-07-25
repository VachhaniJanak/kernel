#include <boot/boot.h>
#include <drivers/screen/screen.h>
#include <platform/attributes.h>
#include <consolefont/font.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/printf.h>
#include <utils/utils.h>
#include <utils/log.h>

static struct FrameBuffer_s fbr;

#define TEXT_MODE_MARGIN_X 5
#define TEXT_MODE_MARGIN_Y 5
#define SPACE_BTW_CHAR 0
#define SPACE_BTW_LINE 2
#define TAB_SIZE 4

static uint8_t font_width = 0;
static uint8_t font_height = 0;

static size_t margin_x = TEXT_MODE_MARGIN_X;
static size_t margin_y = TEXT_MODE_MARGIN_Y;

static size_t space_btw_char = SPACE_BTW_CHAR;
static size_t space_btw_line = SPACE_BTW_LINE;

static size_t screen_x = TEXT_MODE_MARGIN_X;
static size_t screen_y = TEXT_MODE_MARGIN_Y;

static size_t tab_size = TAB_SIZE;

bool init_screen(void) {

  getFramebufferAddr(&fbr);

  if (fbr.address == NULL)
    return false;


  font_width = get_psf_glyph_width();
  font_height = get_psf_glyph_height();

  switch (fbr.bpp) {
  case 32:
    return true;
  case 24:
    return true;
  case 16:
    return true;
  case 8:
    return true;
  default:
    return false;
  }
}

void reset_screen_axis(void) {
  screen_x = margin_x;
  screen_y = margin_y;
}

void set_screen_margin(size_t x, size_t y) {
  margin_x = x;
  margin_y = y;
}

void set_screen_space(size_t char_space, size_t line_space) {
  space_btw_char = char_space;
  space_btw_line = line_space;
}

size_t get_screen_font_width(void) { return font_width; }

size_t get_screen_font_height(void) { return font_height; }

size_t get_screen_tab_size(void) { return tab_size; }

void set_screen_tab_size(size_t size) { tab_size = size; }

size_t get_screen_width(void) { return fbr.width; }

size_t get_screen_height(void) { return fbr.height; }

size_t get_screen_pitch(void) { return fbr.pitch; }

size_t get_screen_bpp(void) { return fbr.bpp; }

size_t get_screen_margin_x(void) { return margin_x; }

size_t get_screen_margin_y(void) { return margin_y; }

size_t get_screen_space_char(void) { return space_btw_char; }

size_t get_screen_space_line(void) { return space_btw_line; }

static inline uint32_t mk_color(uint32_t rgb) {

  uint8_t r = (uint8_t)(rgb >> 16);
  uint8_t g = (uint8_t)(rgb >> 8);
  uint8_t b = (uint8_t)(rgb >> 0);

  uint8_t rcolor = (r >> (8 - fbr.red_mask_size));
  uint8_t gcolor = (g >> (8 - fbr.green_mask_size));
  uint8_t bcolor = (b >> (8 - fbr.blue_mask_size));

  uint32_t color = (rcolor << fbr.red_mask_shift) |
                   (gcolor << fbr.green_mask_shift) |
                   (bcolor << fbr.blue_mask_shift);
  return color;
}

static inline void w32bitpx(void *addr, uint32_t color) {
  *(uint32_t *)addr = color;
}

static inline void w24bitpx(void *addr, uint32_t color) {
  uint32_t *pixel = (uint32_t *)addr;
  *pixel = (color & 0xffffff) | (*pixel & 0xff000000);
}

static inline void w16bitpx(void *addr, uint32_t color) {
  *(uint16_t *)addr = (uint16_t)color;
}

static inline void w8bitpx(void *addr, uint32_t color) {
  *(uint8_t *)addr = (uint8_t)color;
}

static inline void clear(uint32_t rgb,
                         void (*px_callback)(void *addr, uint32_t color)) {

  uint32_t color = mk_color(rgb);
  uint8_t *addr = (uint8_t *)fbr.address;
  uint32_t bypp = fbr.bpp / 8;

  for (uint32_t y = 0; y < fbr.height; y++)
    for (uint32_t x = 0; x < fbr.width; x++)
      px_callback(addr + y * fbr.pitch + x * bypp, color);
}

void clear_screen(uint32_t rgb) {

  switch (fbr.bpp) {

  case 32:
    clear(rgb, &w32bitpx);
    return;
  case 24:
    clear(rgb, &w24bitpx);
    return;
  case 16:
    clear(rgb, &w16bitpx);
    return;
  case 8:
    clear(rgb, &w8bitpx);
    return;

  default:;
  }
}

static inline void _draw_char(screenChar_t *character,
                              void (*px_callback)(void *addr, uint32_t color)) {

  uint8_t *addr = (uint8_t *)fbr.address;
  uint32_t bypp = fbr.bpp / 8;

  uint32_t bg_color = mk_color(character->bg_rgb);
  uint32_t fg_color = mk_color(character->fg_rgb);

  uint32_t y = character->y_position;
  uint32_t x = character->x_position;
  uint32_t bytes_per_row = (font_width + 7) / 8;

  for (int row = 0; row < font_height; row++, y++) {
    x = character->x_position;
   
    for (uint32_t byte = 0; byte < bytes_per_row; byte++) {  
      size_t idx = row * bytes_per_row + byte;
      uint8_t bits = get_psf_glyphs(character->character)[idx];

      for (int bit = 0; bit < 8; bit++, x++) {
        if (bits & (0x80 >> bit))
          px_callback(addr + y * fbr.pitch + x * bypp, fg_color);
        else
          px_callback(addr + y * fbr.pitch + x * bypp, bg_color);
      }
    }
  }
}

void screen_draw_char(screenChar_t *character) {

  switch (fbr.bpp) {

  case 32:
    _draw_char(character, &w32bitpx);
    return;
  case 24:
    _draw_char(character, &w24bitpx);
    return;
  case 16:
    _draw_char(character, &w16bitpx);
    return;
  case 8:
    _draw_char(character, &w8bitpx);
    return;

  default:;
  }
}

static inline void move_one_lineup(void) {

  uint8_t *addr = (uint8_t *)fbr.address;
  size_t offset = font_height + space_btw_line;

  for (size_t y = offset + margin_y; y < fbr.height - offset; y += offset) {
    uint8_t *src = addr + y * fbr.pitch;
    uint8_t *dst = addr + (y - offset) * fbr.pitch;

    kmemmove(dst, src, fbr.pitch * offset);
    kmemset(src, 0, fbr.pitch * offset);
  }
}

static inline void need_scroll(void) {
  if (screen_y + font_height + margin_y >= fbr.height) {
    move_one_lineup();
    screen_y -= font_height + space_btw_line;
  }
}

static inline void set_new_line(void) {
  screen_y += font_height + space_btw_line;
  screen_x = margin_x;
  need_scroll();
}

static inline void carrage_return(void) {
  uint8_t *addr = (uint8_t *)fbr.address;
  size_t offset = screen_y * fbr.pitch;
  size_t num_bytes = fbr.pitch * (font_height + space_btw_line);
  
  screen_x = margin_x;

  kmemset(addr + offset, 0, num_bytes);
}

static inline void apply_tab(void){
  screen_x += tab_size * (font_width + space_btw_char);
  
  if (screen_x >= fbr.width) {
    set_new_line();
  }
}

inline void _putchar(char c){
  if (c == '\n') {
    set_new_line();
    return;
  }

  if (c == '\r') {
    carrage_return();
    return;
  }

  if (c == '\t') {
    apply_tab();
    return;
  }

  if ((screen_x + font_width + margin_x) >= fbr.width){
    set_new_line();
  }

  screenChar_t character = {.x_position = screen_x,
                            .y_position = screen_y,
                            .fg_rgb = 0xffffffff,
                            .bg_rgb = 0xff000000,
                            .character = c};

  screen_draw_char(&character);
  screen_x += font_width + space_btw_char;
}

static inline void putchar(char c, void *arg) {
  UNUSED(arg);
  _putchar(c);
}

void screen_putchar(char c) {
  _putchar(c);
}

static inline void _rmchar(void) {
  if (screen_x == margin_x && screen_y == margin_y){
    return;
  }

  if (screen_x == margin_x) {
    screen_y -= font_height + space_btw_line;
    screen_x = fbr.width - font_width - margin_x;
    return;
  } 

  screen_x -= font_width + space_btw_char;
}

void screen_rmchar(size_t count) {
  for (size_t i = 0; i < count; i++){
    _rmchar();
    _putchar(' ');
    _rmchar();
  }
}

void screen_rmline(size_t count){
  for (size_t i = 0; i < count && screen_y > margin_y; i++){
    if (screen_y == margin_y){
      return;
    }

    carrage_return();
    screen_y -= font_height + space_btw_line;
  }
}

int kprintf(const char *format, ...) {

  va_list va;
  va_start(va, format);

  int ret = vfctprintf(putchar, NULL, format, va);

  va_end(va);
  return ret;
}
