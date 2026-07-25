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
  KBD_STATE_NORMAL,
  KBD_STATE_BREAK,
  KBD_STATE_EXTENDED,
  KBD_STATE_EXTENDED_BREAK,
  KBD_STATE_PAUSE
} keyboard_state_t;

typedef enum { KEY_EVENT_PRESS, KEY_EVENT_RELEASE } key_action_t;

typedef struct {
  bool extended;
  key_action_t action;
  uint8_t scancode;
} keyboard_event_t;

void ps2_init(void);

bool ps2_get_key_event(keyboard_event_t* event);
