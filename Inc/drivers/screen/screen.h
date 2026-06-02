#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  size_t x_position;
  size_t y_position;
  uint32_t bg_rgb;
  uint32_t fg_rgb;
  char character;
} screenChar_t;

bool init_screen(void);

void reset_screen_axis(void);

void set_screen_margin(size_t x, size_t y);

void set_screen_space(size_t char_space, size_t line_space);

size_t get_screen_width(void);

size_t get_screen_height(void);

size_t get_screen_pitch(void);

size_t get_screen_bpp(void);

size_t get_screen_margin_x(void);

size_t get_screen_margin_y(void);

size_t get_screen_space_char(void);

size_t get_screen_space_line(void);

void clear_screen(uint32_t rgb);

void screen_draw_char(screenChar_t *character);

int kprintf(const char *format, ...);
