#pragma once

#include <process/waitqueue.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  KBD_MODIFIER_LSHIFT = 0x01,
  KBD_MODIFIER_RSHIFT = 0x02,
  KBD_MODIFIER_LCTRL = 0x04,
  KBD_MODIFIER_RCTRL = 0x08,
  KBD_MODIFIER_LALT = 0x10,
  KBD_MODIFIER_RALT = 0x20,
  KBD_CAPSLOCK_ACTIVE = 0x40,
  KBD_NUMLOCK_ACTIVE = 0x80,
  KBD_SCROLLLOCK_ACTIVE = 0x100
} kbd_key_modifier_t;

typedef struct {
  uint32_t unicode;    // (e.g., 'A', or 0 if it's an arrow key)
  uint8_t keycode;     // The physical grid coordinate (e.g., KEY_R3_C2)
  uint16_t modifiers;  // Bitmask for modifier keys (Shift, Ctrl, Alt, etc.)
  bool is_pressed;     // True if pressed, False if released
} kbd_key_event_t;

extern waitqueue_t kbd_waitqueue;

void keyboard_init(void);

void keyboard_process_key_handler(uint8_t keycode, uint16_t modifiers,
                                  bool is_pressed);

bool kbd_dqueue_key_event(kbd_key_event_t* event);
