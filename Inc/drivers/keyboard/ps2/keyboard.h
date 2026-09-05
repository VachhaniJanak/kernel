#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64

#define PS2_CMD_READ_CFG 0x20
#define PS2_CMD_WRITE_CFG 0x60

#define PS2_CMD_GS_SCANCODE 0xF0

#define PS2_CMD_GET_CURRENT_SET 0x0

#define PS2_CMD_SET_SCANCODE_SET_1 0x1
#define PS2_CMD_SET_SCANCODE_SET_2 0x2
#define PS2_CMD_SET_SCANCODE_SET_3 0x3

#define PS2_KEYBOARD_IRQ_EN_BIT 0
#define PS2_MOUSE_IRQ_EN_BIT 1
#define PS2_SYS_PASS_PORT_BIT 2
#define PS2_KEYBOARD_CLK_BIT 4
#define PS2_MOUSE_CLK_BIT 5
#define PS2_TRANSLATION_EN_BIT 6

#define PS2_SCANCODE_SET_1 1
#define PS2_SCANCODE_SET_2 2
#define PS2_SCANCODE_SET_3 3

#define PS2_SCANCODE_SET_1_VAL 0x43
#define PS2_SCANCODE_SET_2_VAL 0x41
#define PS2_SCANCODE_SET_3_VAL 0x3f

typedef enum {
  PS2_KBD_STATE_NORMAL,
  PS2_KBD_STATE_BREAK,
  PS2_KBD_STATE_EXTENDED,
  PS2_KBD_STATE_EXTENDED_BREAK,
  PS2_KBD_STATE_PAUSE
} ps2_kbd_sc_state_t;

struct ps2_kbd_state_s {
  ps2_kbd_sc_state_t state;
  bool keys_pressed[256];
  bool toggle_caps;
  bool toggle_num;
  bool toggle_scroll;
  bool left_shift_pressed;
  bool right_shift_pressed;
  bool left_ctrl_pressed;
  bool right_ctrl_pressed;
  bool left_alt_pressed;
  bool right_alt_pressed;
};

void ps2_init(void);
