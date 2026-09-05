#include <arch/x86_64/timer.h>
#include <consolefont/font.h>
#include <drivers/screen/screen.h>
#include <platform/attributes.h>
#include <process/locks.h>
#include <process/thread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <tty/tty_output.h>
#include <utils/log.h>
#include <utils/printf.h>
#include <utils/utils.h>

#define OUTPUT_BUFFER_SIZE 1024

static const uint32_t ansi_colors[8] = {
    0x000000, 0xCC0000, 0x4E9A06, 0xC4A000,  // Black, Red, Green, Yellow
    0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF   // Blue, Magenta, Cyan, White
};

static const uint32_t ansi_bright_colors[8] = {
    0x555753, 0xEF2929, 0x8AE234, 0xFCE94F,  // Bright Black, Red, Green, Yellow
    0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC  // Bright Blue, Magenta, Cyan, White
};

static struct tty_output_state_s tty_state = {0};

static mutex_t tty_output_lock = {0};
static uint8_t tty_output_buffer[OUTPUT_BUFFER_SIZE] = {0};
static uint16_t head = 0;
static uint16_t tail = 0;

void tty_init_output(void) {
  mutex_init(&tty_output_lock);

  // screen
  tty_state.screen_width = screen_get_screen_width();
  tty_state.screen_height = screen_get_screen_height();
  tty_state.screen_pitch = screen_get_screen_pitch();
  tty_state.framebuffer = (uint8_t*)screen_get_framebuffer_addr();
  tty_state.bpp = screen_get_screen_bpp();

  uint8_t* ptr = consolefont_get_glyphs_ptr();
  size_t bytes = consolefont_get_bytes_per_glyph();

  screen_set_screen_glyphs_ptr(ptr);
  screen_set_screen_bytes_per_glyph(bytes);

  tty_state.cursor_x = TTY_PADDING_LEFT;
  tty_state.cursor_y = TTY_PADDING_TOP;

  tty_state.cursor_y += TTY_BORDER_THICKNESS + TTY_BORDER_MARGIN;
  tty_state.cursor_x += TTY_BORDER_THICKNESS + TTY_BORDER_MARGIN;

  tty_state.fg_color = TTY_CHAR_FOREGROUND_COLOR;
  tty_state.bk_color = TTY_CHAR_BACKGROUND_COLOR;
  tty_state.tty_color = TTY_COLOR;

  tty_state.font_width = consolefont_get_glyph_width();
  tty_state.font_height = consolefont_get_glyph_height();

  screen_set_screen_font_height(tty_state.font_height);
  screen_set_screen_font_width(tty_state.font_width);

  // border
  tty_state.border_thickness = TTY_BORDER_THICKNESS;
  tty_state.border_color = TTY_BORDER_COLOR;
  tty_state.border_margin = TTY_BORDER_MARGIN;

  // padding
  tty_state.padding_left = TTY_PADDING_LEFT;
  tty_state.padding_right = TTY_PADDING_RIGHT;
  tty_state.padding_top = TTY_PADDING_TOP;
  tty_state.padding_bottom = TTY_PADDING_BOTTOM;

  // spacing
  tty_state.char_space = TTY_CHAR_SPACING;
  tty_state.line_space = TTY_LINE_SPACING;
  tty_state.tab_size = TTY_TAB_SIZE;

  // content area
  tty_state.content_x_start = tty_state.padding_left;
  tty_state.content_x_start += tty_state.border_thickness;
  tty_state.content_x_start += tty_state.border_margin;

  tty_state.content_x_end = tty_state.screen_width;
  tty_state.content_x_end -= tty_state.padding_right;
  tty_state.content_x_end -= tty_state.border_thickness;
  tty_state.content_x_end -= tty_state.border_margin;

  tty_state.content_y_start = tty_state.padding_top;
  tty_state.content_y_start += tty_state.border_thickness;
  tty_state.content_y_start += tty_state.border_margin;

  tty_state.content_y_end = tty_state.screen_height;
  tty_state.content_y_end -= tty_state.padding_bottom;
  tty_state.content_y_end -= tty_state.border_thickness;
  tty_state.content_y_end -= tty_state.border_margin;

  // ansi state
  tty_state.ansi_state = ANSI_STATE_NORMAL;
  tty_state.ansi_param_count = 0;
  tty_state.ansi_current_val = 0;

  // ansi style flags
  tty_state.style_bold = false;
  tty_state.style_dim = false;
  tty_state.style_italic = false;
  tty_state.style_underline = false;
  tty_state.style_blink = false;
  tty_state.style_inverse = false;

  // Save original colors for "Reset" (\033[0m)
  tty_state.default_fg_color = tty_state.fg_color;
  tty_state.default_bk_color = tty_state.bk_color;
}

static inline bool is_queue_empty(void) { return head == tail; }

static inline bool is_queue_full(void) {
  return (tail + 1) % OUTPUT_BUFFER_SIZE == head;
}

static inline bool enqueue(char c) {
  if (is_queue_full()) {
    return false;
  }

  tty_output_buffer[tail] = c;
  tail = (tail + 1) % OUTPUT_BUFFER_SIZE;
  return true;
}

static inline bool dequeue(char* c) {
  if (is_queue_empty()) {
    return false;
  }

  *c = tty_output_buffer[head];
  head = (head + 1) % OUTPUT_BUFFER_SIZE;
  return true;
}

static inline void draw_vertical_line(uint32_t x_start, uint32_t line_width,
                                      uint32_t y_start, uint32_t y_end,
                                      uint32_t color) {
  screen_draw_fill_rect(x_start, x_start + line_width, y_start, y_end, color);
}

static inline void draw_horizontal_line(uint32_t y_start, uint32_t line_width,
                                        uint32_t x_start, uint32_t x_end,
                                        uint32_t color) {
  screen_draw_fill_rect(x_start, x_end, y_start, y_start + line_width, color);
}

void tty_clear_screen(void) {
  // Clear the screen with the background color
  screen_clear(tty_state.tty_color);

  uint32_t border_color = tty_state.border_color;

  uint32_t screen_height = tty_state.screen_height;
  uint32_t screen_width = tty_state.screen_width;

  uint32_t border_thickness = tty_state.border_thickness;
  uint32_t border_margin = tty_state.border_margin;

  // draw the border rectangle
  draw_vertical_line(border_margin, border_thickness, border_margin,
                     screen_height - border_margin,
                     border_color);  // Left border

  draw_vertical_line(screen_width - border_margin - border_thickness,
                     border_thickness, border_margin,
                     screen_height - border_margin,
                     border_color);  // Right border

  draw_horizontal_line(border_margin, border_thickness, border_margin,
                       screen_width - border_margin,
                       border_color);  // Top border

  draw_horizontal_line(screen_height - border_margin - border_thickness,
                       border_thickness, border_margin,
                       screen_width - border_margin,
                       border_color);  // Bottom border

  tty_state.cursor_x = tty_state.padding_left;
  tty_state.cursor_x += tty_state.border_thickness;
  tty_state.cursor_x += tty_state.border_margin;

  tty_state.cursor_y = tty_state.padding_top;
  tty_state.cursor_y += tty_state.border_thickness;
  tty_state.cursor_y += tty_state.border_margin;
}

static inline void scroll_up(void) {
  uint8_t* fbr = tty_state.framebuffer;
  uint32_t pitch = tty_state.screen_pitch;
  uint32_t bypp = tty_state.bpp / 8;

  uint32_t total_line_height = tty_state.font_height + tty_state.line_space;

  uint32_t content_start_x = tty_state.content_x_start;
  uint32_t content_end_x = tty_state.content_x_end;
  uint32_t content_start_y = tty_state.content_y_start;
  uint32_t content_end_y = tty_state.content_y_end;
  uint32_t content_width_bytes = (content_end_x - content_start_x) * bypp;

  for (uint32_t y = content_start_y; y < content_end_y - total_line_height;
       y++) {
    uint8_t* dest = fbr + (y * pitch) + (content_start_x * bypp);
    uint8_t* src =
        fbr + ((y + total_line_height) * pitch) + (content_start_x * bypp);

    // Copy exactly one row of pixels inside the content area
    kmemmove(dest, src, content_width_bytes);
  }

  // Clear the bottom line using your proper color fill!
  uint32_t erase_start_y = content_end_y - total_line_height;
  screen_draw_fill_rect(content_start_x, content_end_x, erase_start_y,
                        content_end_y, tty_state.tty_color);
}

static inline void need_scroll_up(void) {
  uint32_t total_line_height = tty_state.font_height + tty_state.line_space;

  // Check if we need to scroll up
  if (tty_state.cursor_y + total_line_height >= tty_state.content_y_end) {
    scroll_up();

    // move to old cursor_y position after scrolling up
    tty_state.cursor_y -= total_line_height;
  }
}

static inline void goto_new_line(void) {
  tty_state.cursor_x = tty_state.content_x_start;
  tty_state.cursor_y += tty_state.font_height;
  tty_state.cursor_y += tty_state.line_space;

  need_scroll_up();
}

static inline void handle_tab(void) {
  uint32_t tab_size_px = tty_state.tab_size * tty_state.font_width;
  uint32_t current_offset = tty_state.cursor_x - tty_state.content_x_start;

  // How far to the NEXT tab stop
  uint32_t pixels_to_next_stop = tab_size_px - (current_offset % tab_size_px);
  tty_state.cursor_x += pixels_to_next_stop;

  if (tty_state.cursor_x >= tty_state.content_x_end) {
    goto_new_line();
  }
}

static inline void handle_backspace(void) {
  uint32_t total_width = tty_state.font_width + tty_state.char_space;
  uint32_t total_height = tty_state.font_height + tty_state.line_space;

  // If we are not at the very left edge, move back one character
  if (tty_state.cursor_x >= tty_state.content_x_start + total_width) {
    tty_state.cursor_x -= total_width;
    return;
  }

  // Wrap back up to the previous line
  if (tty_state.cursor_y >= tty_state.content_y_start + total_height) {
    tty_state.cursor_y -= total_height;

    // Put cursor at the very end of the previous line
    uint32_t content_width =
        tty_state.content_x_end - tty_state.content_x_start;

    // Find the dead pixels on the right that couldn't fit a full character
    uint32_t dead_zone = content_width % total_width;

    tty_state.cursor_x = tty_state.content_x_end;
    tty_state.cursor_x -= dead_zone;    // Align to the last character boundary
    tty_state.cursor_x -= total_width;  // Move back one character
  }
}

static inline void apply_ansi_sgr(void) {
  // If no params, default is 0 (Reset)
  if (tty_state.ansi_param_count == 0 ||
      (tty_state.ansi_param_count == 1 && tty_state.ansi_params[0] == 0)) {
    tty_state.style_bold = false;
    tty_state.style_dim = false;
    tty_state.style_italic = false;
    tty_state.style_underline = false;
    tty_state.style_inverse = false;

    tty_state.fg_color = tty_state.default_fg_color;
    tty_state.bk_color = tty_state.default_bk_color;
    return;
  }

  for (uint32_t i = 0; i < tty_state.ansi_param_count; i++) {
    uint32_t code = tty_state.ansi_params[i];

    if (code == 1) {
      tty_state.style_bold = true;
    } else if (code == 2) {
      tty_state.style_dim = true;
    } else if (code == 3) {
      tty_state.style_italic = true;
    } else if (code == 4) {
      tty_state.style_underline = true;
    } else if (code == 5) {
      tty_state.style_blink = true;
    } else if (code == 7) {
      tty_state.style_inverse = true;
    } else if (code == 22) {
      tty_state.style_bold = false;
      tty_state.style_dim = false;
    } else if (code == 24) {
      tty_state.style_underline = false;
    } else if (code == 27) {
      tty_state.style_inverse = false;
    }

    // Foreground Colors (30-37)
    else if (code >= 30 && code <= 37) {
      tty_state.fg_color = ansi_colors[code - 30];
    }

    // Background Colors (40-47)
    else if (code >= 40 && code <= 47) {
      tty_state.bk_color = ansi_colors[code - 40];
    }

    // Foreground Bright Colors (90-97)
    else if (code >= 90 && code <= 97) {
      tty_state.fg_color = ansi_bright_colors[code - 90];
    }

    // Background Bright Colors (100-107)
    else if (code >= 100 && code <= 107) {
      tty_state.bk_color = ansi_bright_colors[code - 100];
    }
  }
}

static inline void _tty_putchar(char c) {
  // ansi state machine
  switch (tty_state.ansi_state) {
    case ANSI_STATE_NORMAL:
      if (c == '\033') {  // ESC character
        tty_state.ansi_state = ANSI_STATE_ESC_RECEIVED;
        return;
      }
      break;

    case ANSI_STATE_ESC_RECEIVED:
      // '\033[' Control Sequence Introducer (CSI)
      if (c == '[') {
        tty_state.ansi_state = ANSI_STATE_CSI_RECEIVED;
        tty_state.ansi_param_count = 0;
        tty_state.ansi_current_val = 0;

        for (int i = 0; i < MAX_ANSI_PARAMS; i++) {
          tty_state.ansi_params[i] = 0;
        }

        return;
      }

      tty_state.ansi_state = ANSI_STATE_NORMAL;
      return;

    case ANSI_STATE_CSI_RECEIVED:
      if (c >= '0' && c <= '9') {
        // Build the number (e.g., '3' then '1' becomes 31)
        tty_state.ansi_current_val =
            (tty_state.ansi_current_val * 10) + (c - '0');
        return;
      }

      if (c == ';') {
        // Store the current parameter and reset for the next one
        if (tty_state.ansi_param_count < MAX_ANSI_PARAMS) {
          tty_state.ansi_params[tty_state.ansi_param_count++] =
              tty_state.ansi_current_val;
        }

        tty_state.ansi_current_val = 0;
        return;
      }

      if (tty_state.ansi_param_count < MAX_ANSI_PARAMS) {
        tty_state.ansi_params[tty_state.ansi_param_count++] =
            tty_state.ansi_current_val;
      }

      // If the command is 'm', apply the Graphics Rendition
      if (c == 'm') {
        apply_ansi_sgr();
      }

      // Reset state machine back to normal text
      tty_state.ansi_state = ANSI_STATE_NORMAL;
      return;

    default:
      tty_state.ansi_state = ANSI_STATE_NORMAL;
      break;
  }

  // Intercept Control Characters
  switch (c) {
    case '\n':  // 0x0A: Line Feed
      goto_new_line();
      return;

    case '\r':  // 0x0D: Carriage Return
      tty_state.cursor_x = tty_state.content_x_start;
      return;

    case '\f':  // 0x0C: Form Feed
      tty_clear_screen();
      return;

    case '\v':  // 0x0B: Vertical Tab
      tty_state.cursor_y += (tty_state.font_height + tty_state.line_space);
      need_scroll_up();
      return;

    case '\t':  // 0x09: Horizontal Tab
      handle_tab();
      return;

    case '\b':  // 0x08: Backspace
      handle_backspace();
      return;

    case '\0':  // 0x00: Null terminator (ignore)
      return;
  }

  // Wrap text IF the next character would bleed into the right margin.
  if (tty_state.cursor_x + tty_state.font_width + tty_state.char_space >=
      tty_state.content_x_end) {
    goto_new_line();
  }

  // Draw the character at the current cursor position
  screen_draw_char(c, tty_state.cursor_x, tty_state.cursor_y,
                   tty_state.bk_color, tty_state.fg_color);

  if (tty_state.style_bold) {
    // Draw bold by drawing the character again with a 1-pixel right offset
    screen_draw_char(c, tty_state.cursor_x + 1, tty_state.cursor_y,
                     tty_state.bk_color, tty_state.fg_color);
  }

  if (tty_state.style_underline) {
    // Draw underline
    uint32_t underline_y = tty_state.cursor_y + tty_state.font_height - 1;

    screen_draw_fill_rect(tty_state.cursor_x,
                          tty_state.cursor_x + tty_state.font_width,
                          underline_y, underline_y + 1, tty_state.fg_color);
  }

  // Move the cursor forward
  tty_state.cursor_x += tty_state.font_width + tty_state.char_space;
}

static inline void _tty_putchar_wrap(char c, void* arg) {
  UNUSED(arg);
  mutex_acquire(&tty_output_lock);
  enqueue(c);
  mutex_release(&tty_output_lock);
}

void tty_putchar(char c) { _tty_putchar_wrap(c, NULL); }

void tty_write(const char* str) {
  mutex_acquire(&tty_output_lock);

  while (*str) {
    enqueue(*str++);
  }

  mutex_release(&tty_output_lock);
}

int tty_printf(const char* format, ...) {
  va_list va;
  va_start(va, format);

  int ret = vfctprintf(_tty_putchar_wrap, NULL, format, va);

  va_end(va);
  return ret;
}

int tty_write_buffer(const char* buffer, size_t length) {
  mutex_acquire(&tty_output_lock);

  int count = 0;

  for (size_t i = 0; i < length; i++) {
    if (enqueue(buffer[i])) {
      count++;
      continue;
    }
    break;  // Buffer is full, stop writing
  }

  mutex_release(&tty_output_lock);
  return count;
}

static inline void tty_blink_cursor(bool force_hide) {
  static bool cursor_visible = true;

  if (cursor_visible && !force_hide) {
    // Draw the cursor as a filled rectangle
    screen_draw_char('_', tty_state.cursor_x, tty_state.cursor_y,
                     tty_state.bk_color, tty_state.fg_color);
  } else {
    // Erase the cursor by redrawing the character at the cursor position
    screen_draw_char(' ', tty_state.cursor_x, tty_state.cursor_y,
                     tty_state.bk_color, tty_state.fg_color);
  }

  cursor_visible = !cursor_visible;
}

void tty_output_thread(void* arg) {
  UNUSED(arg);

  static timer_tick_t blink_timer = 0;
  blink_timer = timer_get_ticks();

  while (true) {
    char c;

    if (dequeue(&c)) {
      tty_blink_cursor(true);
      _tty_putchar(c);
      continue;  // Skip the sleep if we processed a character
    }

    // If the queue is empty, we can blink the cursor
    const timer_tick_t current_ticks = timer_get_ticks();
    const timer_tick_t blink_interval_ticks = timer_ms_to_ticks(500);

    if (timer_get_delta_tick(blink_timer, current_ticks) >=
        blink_interval_ticks) {
      tty_blink_cursor(false);
      blink_timer = timer_get_ticks();
    }

    kthread_sleep(100);
  }
}
