#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TEXT_MODE_MARGIN_X 4
#define TEXT_MODE_MARGIN_Y 4
#define SPACE_BTW_CHAR 3
#define SPACE_BTW_LINE 4

typedef struct
{
    uint32_t x_pos;
    uint32_t y_pos;
    uint32_t bg_rgb;
    uint32_t fg_rgb;
    char ch;
} DrawCharTypedef;

bool init_screen(unsigned long mbi_addr);
void clr_screen(uint32_t rgb);
void draw_char(DrawCharTypedef draw_char);
