#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TTY_TEXT_MODE_MARGIN_X 5
#define TTY_TEXT_MODE_MARGIN_Y 5
#define TTY_SPACE_BTW_CHAR 0
#define TTY_SPACE_BTW_LINE 2
#define TTY_TAB_SIZE 4

// TTY Styling configurations (in pixels)
#define TTY_BORDER_THICKNESS 3
#define TTY_BORDER_COLOR 0xcbcbcb  // White
#define TTY_BORDER_MARGIN 16

#define TTY_PADDING_LEFT 8
#define TTY_PADDING_RIGHT 8
#define TTY_PADDING_TOP 8
#define TTY_PADDING_BOTTOM 8

#define TTY_CHAR_SPACING 0  // Pixels between each letter
#define TTY_LINE_SPACING 0  // Pixels between each row of text

#define TTY_CHAR_FOREGROUND_COLOR 0xcbcbcb  // White
#define TTY_CHAR_BACKGROUND_COLOR 0x07131A  // Black
#define TTY_COLOR 0x07131A                  // Black

#define MAX_ANSI_PARAMS 8

enum ansi_state_e {
  ANSI_STATE_NORMAL = 0,
  ANSI_STATE_ESC_RECEIVED,
  ANSI_STATE_CSI_RECEIVED
};

struct tty_output_state_s {
  // screen
  uint32_t screen_width;
  uint32_t screen_height;
  uint32_t screen_pitch;
  uint8_t* framebuffer;
  uint32_t bpp;

  uint32_t cursor_x;
  uint32_t cursor_y;
  uint32_t fg_color;   // char foreground color
  uint32_t bk_color;   // char background color
  uint32_t tty_color;  // overall TTY background color
  uint16_t font_width;
  uint16_t font_height;

  // border
  uint32_t border_thickness;
  uint32_t border_color;
  uint32_t border_margin;

  // padding (inside the border)
  uint32_t padding_left;
  uint32_t padding_right;
  uint32_t padding_top;
  uint32_t padding_bottom;

  // content area (inside the padding)
  uint32_t content_x_start;
  uint32_t content_x_end;
  uint32_t content_y_start;
  uint32_t content_y_end;

  // spacing
  uint32_t char_space;
  uint32_t line_space;
  uint32_t tab_size;

  // ansi state
  enum ansi_state_e ansi_state;
  uint32_t ansi_params[MAX_ANSI_PARAMS];
  uint32_t ansi_param_count;
  uint32_t ansi_current_val;

  // ansi style flags
  bool style_bold;
  bool style_dim;
  bool style_italic;
  bool style_underline;
  bool style_blink;
  bool style_inverse;

  // Save original colors for "Reset" (\033[0m)
  uint32_t default_fg_color;
  uint32_t default_bk_color;
};

void tty_init_output(void);

void tty_clear_screen(void);

void tty_putchar(char c);

void tty_write(const char* str);

int tty_printf(const char* format, ...);

void tty_output_thread(void* arg);

int tty_write_buffer(const char* buffer, size_t length);
