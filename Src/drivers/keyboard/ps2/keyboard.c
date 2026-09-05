#include <arch/x86_64/apic.h>
#include <arch/x86_64/io.h>
#include <drivers/keyboard/ps2/keyboard.h>
#include <input/keyboard/keyboard.h>
#include <input/keyboard/keycode.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

#include "scancode_set2.h"

static struct ps2_kbd_state_s keyboard_state = {0};

void ps2_init(void) {
  // Initialization code for PS/2 keyboard

  // Check which scancode set is currently active
  outb(PS2_DATA_PORT, PS2_CMD_GS_SCANCODE);
  outb(PS2_DATA_PORT, PS2_CMD_GET_CURRENT_SET);

  volatile uint8_t byte1 = inb(PS2_DATA_PORT);
  volatile uint8_t byte2 = inb(PS2_DATA_PORT);

  if (byte1 != 0xFA) {
    LOG_ERROR("Failed to get current scancode set\n");
    return;
  }
  if (byte2 != PS2_SCANCODE_SET_2_VAL) {
#ifdef PS2_DEBUG
    LOG_DEBUG("Current scancode is 0x%02x, setting to set 2\n", byte2);
#endif
    outb(PS2_DATA_PORT, PS2_CMD_GS_SCANCODE);
    byte1 = inb(PS2_DATA_PORT);

    if (byte1 != 0xFA) {
      LOG_ERROR("Failed to set scancode set to set 2\n");
      return;
    }

    outb(PS2_DATA_PORT, PS2_CMD_SET_SCANCODE_SET_2);
    byte2 = inb(PS2_DATA_PORT);

    if (byte2 != 0xFA) {
      LOG_ERROR("Failed to set scancode set to set 2\n");
      return;
    }
  }

  // check is translation enabled
  // keyboard IRQ is enabled, if not enable them
  outb(PS2_STATUS_PORT, PS2_CMD_READ_CFG);
  volatile uint8_t cfg = inb(PS2_DATA_PORT);

  if (cfg & (1 << PS2_TRANSLATION_EN_BIT)) {
#ifdef PS2_DEBUG
    LOG_DEBUG("PS/2 translation is enabled\n");
#endif

    cfg |= (1 << PS2_KEYBOARD_IRQ_EN_BIT);  // enable keyboard IRQ
    cfg &= ~(1 << PS2_TRANSLATION_EN_BIT);  // disable translation

    outb(PS2_STATUS_PORT, PS2_CMD_WRITE_CFG);
    outb(PS2_DATA_PORT, cfg);

#ifdef PS2_DEBUG
    outb(PS2_STATUS_PORT, PS2_CMD_READ_CFG);
    uint8_t cfg = inb(PS2_DATA_PORT);
    LOG_DEBUG("PS/2 configuration after update: 0x%02x\n", cfg);
#endif
  }

#ifdef PS2_DEBUG
  LOG_DEBUG("PS/2 initialized\n");
#endif

  // Initialize the keyboard state
  keyboard_state.state = PS2_KBD_STATE_NORMAL;
  keyboard_state.toggle_caps = false;
  keyboard_state.toggle_num = false;
  keyboard_state.toggle_scroll = false;
  keyboard_state.left_shift_pressed = false;
  keyboard_state.right_shift_pressed = false;
  keyboard_state.left_ctrl_pressed = false;
  keyboard_state.right_ctrl_pressed = false;
  keyboard_state.left_alt_pressed = false;
  keyboard_state.right_alt_pressed = false;

  for (int i = 0; i < 256; i++) {
    keyboard_state.keys_pressed[i] = false;
  }
}

static inline void update_keyboard_state(struct ps2_kbd_state_s* kbd_state,
                                         uint8_t keycode, bool is_pressed) {
  switch (keycode) {
    case KEY_LSHIFT:
      kbd_state->left_shift_pressed = is_pressed;
      return;

    case KEY_RSHIFT:
      kbd_state->right_shift_pressed = is_pressed;
      return;

    case KEY_LCTRL:
      kbd_state->left_ctrl_pressed = is_pressed;
      return;

    case KEY_RCTRL:
      kbd_state->right_ctrl_pressed = is_pressed;
      return;

    case KEY_LALT:
      kbd_state->left_alt_pressed = is_pressed;
      return;

    case KEY_RALT:
      kbd_state->right_alt_pressed = is_pressed;
      return;

    case KEY_CAPSLOCK:
      if (is_pressed) {
        kbd_state->toggle_caps = !kbd_state->toggle_caps;
      }

      return;

    case KEY_NUM_LOCK:
      if (is_pressed) {
        kbd_state->toggle_num = !kbd_state->toggle_num;
      }

      return;

    case KEY_SCROLLLOCK:
      if (is_pressed) {
        kbd_state->toggle_scroll = !kbd_state->toggle_scroll;
      }

      return;

    default:
      return;
  }
}

static inline uint16_t get_modifiers(struct ps2_kbd_state_s* kbd_state) {
  uint16_t modifiers = 0;

  if (kbd_state->left_shift_pressed) {
    modifiers |= KBD_MODIFIER_LSHIFT;
  }

  if (kbd_state->right_shift_pressed) {
    modifiers |= KBD_MODIFIER_RSHIFT;
  }

  if (kbd_state->left_ctrl_pressed) {
    modifiers |= KBD_MODIFIER_LCTRL;
  }

  if (kbd_state->right_ctrl_pressed) {
    modifiers |= KBD_MODIFIER_RCTRL;
  }

  if (kbd_state->left_alt_pressed) {
    modifiers |= KBD_MODIFIER_LALT;
  }

  if (kbd_state->right_alt_pressed) {
    modifiers |= KBD_MODIFIER_RALT;
  }

  if (kbd_state->toggle_caps) {
    modifiers |= KBD_CAPSLOCK_ACTIVE;
  }

  if (kbd_state->toggle_num) {
    modifiers |= KBD_NUMLOCK_ACTIVE;
  }

  if (kbd_state->toggle_scroll) {
    modifiers |= KBD_SCROLLLOCK_ACTIVE;
  }

  return modifiers;
}
static inline void scancode_helper(struct ps2_kbd_state_s* kbd_state,
                                   uint8_t scancode, bool released,
                                   bool extended, const uint8_t* sc_to_kc,
                                   const uint8_t* ext_sc_to_kc) {
  if (kbd_state->keys_pressed[scancode] && !released) {
    // Key is already pressed, ignore the event
    return;
  }

  uint8_t key_code;

  if (extended) {
    key_code = ext_sc_to_kc[scancode];
  } else {
    key_code = sc_to_kc[scancode];
  }

  // key is released
  if (released) {
    kbd_state->keys_pressed[scancode] = false;
    update_keyboard_state(kbd_state, key_code, false);

    uint16_t modifiers = get_modifiers(kbd_state);
    keyboard_process_key_handler(key_code, modifiers, false);
    return;
  }

  // Key is pressed
  kbd_state->keys_pressed[scancode] = true;
  update_keyboard_state(kbd_state, key_code, true);

  uint16_t modifiers = get_modifiers(kbd_state);
  keyboard_process_key_handler(key_code, modifiers, true);
}

static inline void process_scancode2(struct ps2_kbd_state_s* kbd_state,
                                     uint8_t byte, const uint8_t* sc_to_kc,
                                     const uint8_t* ext_sc_to_kc) {
  switch (kbd_state->state) {
    case PS2_KBD_STATE_NORMAL:

      if (byte == 0xF0) {
        kbd_state->state = PS2_KBD_STATE_BREAK;
        return;
      }

      if (byte == 0xE0) {
        kbd_state->state = PS2_KBD_STATE_EXTENDED;
        return;
      }

      if (byte == 0xE1) {
        kbd_state->state = PS2_KBD_STATE_PAUSE;
        return;
      }

      scancode_helper(kbd_state, byte, false, false, sc_to_kc, ext_sc_to_kc);
      return;

    case PS2_KBD_STATE_BREAK:
      scancode_helper(kbd_state, byte, true, false, sc_to_kc, ext_sc_to_kc);
      kbd_state->state = PS2_KBD_STATE_NORMAL;
      return;

    case PS2_KBD_STATE_EXTENDED:
      if (byte == 0xF0) {
        kbd_state->state = PS2_KBD_STATE_EXTENDED_BREAK;
        return;
      }

      scancode_helper(kbd_state, byte, false, true, sc_to_kc, ext_sc_to_kc);
      kbd_state->state = PS2_KBD_STATE_NORMAL;
      return;

    case PS2_KBD_STATE_EXTENDED_BREAK:
      scancode_helper(kbd_state, byte, true, true, sc_to_kc, ext_sc_to_kc);
      kbd_state->state = PS2_KBD_STATE_NORMAL;
      return;

    case PS2_KBD_STATE_PAUSE:
      kbd_state->state = PS2_KBD_STATE_NORMAL;
      return;
  }
}

void keyboard_irq_isr_handler(void) {
  uint8_t scancode = inb(PS2_DATA_PORT);
  process_scancode2(&keyboard_state, scancode, scancode_set2_to_keycode,
                    extended_scancode_set2_to_keycode);
  lapic_eoi();
}
