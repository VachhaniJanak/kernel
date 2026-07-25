#include <arch/x86_64/apic.h>
#include <arch/x86_64/io.h>
#include <drivers/keyboard/ps.h>
#include <drivers/screen/screen.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

#define PS2_BUFFER_SIZE 256

static keyboard_state_t state = KBD_STATE_NORMAL;
static keyboard_event_t buffer[PS2_BUFFER_SIZE];
static bool keys_pressed[256] = {false};

static uint8_t head = 0;
static uint8_t tail = 0;

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
  state = KBD_STATE_NORMAL;
}

static inline bool is_queue_empty(void) { return head == tail; }

static inline bool is_queue_full(void) {
  return (tail + 1) % PS2_BUFFER_SIZE == head;
}

static inline bool enqueue(keyboard_event_t event) {
  if (is_queue_full()) {
    return false;
  }

  buffer[tail] = event;
  tail = (tail + 1) % PS2_BUFFER_SIZE;
  return true;
}

static inline bool dequeue(keyboard_event_t* event) {
  if (is_queue_empty()) {
    return false;
  }

  *event = buffer[head];
  head = (head + 1) % PS2_BUFFER_SIZE;
  return true;
}

static inline void generate_key_event(uint8_t scancode, bool released,
                                      bool extended) {
  if (keys_pressed[scancode] && !released) {
    // Key is already pressed, ignore the event
    return;
  }

  keyboard_event_t event;

  event.scancode = scancode;
  event.extended = extended;

  if (released) {
    keys_pressed[scancode] = false;
    event.action = KEY_EVENT_RELEASE;
  } else {
    keys_pressed[scancode] = true;
    event.action = KEY_EVENT_PRESS;
  }

  enqueue(event);
}

static inline void keyboard_process(uint8_t byte) {
  switch (state) {
    case KBD_STATE_NORMAL:

      if (byte == 0xF0) {
        state = KBD_STATE_BREAK;
        return;
      }

      if (byte == 0xE0) {
        state = KBD_STATE_EXTENDED;
        return;
      }

      if (byte == 0xE1) {
        state = KBD_STATE_PAUSE;
        return;
      }

      generate_key_event(byte, false, false);
      return;

    case KBD_STATE_BREAK:
      generate_key_event(byte, true, false);
      state = KBD_STATE_NORMAL;
      return;

    case KBD_STATE_EXTENDED:

      if (byte == 0xF0) {
        state = KBD_STATE_EXTENDED_BREAK;
        return;
      }

      generate_key_event(byte, false, true);
      state = KBD_STATE_NORMAL;
      return;

    case KBD_STATE_EXTENDED_BREAK:
      generate_key_event(byte, true, true);
      state = KBD_STATE_NORMAL;
      return;

    case KBD_STATE_PAUSE:
      // Handle separately
      break;
  }
}

void keyboard_irq_isr_handler(void) {
  volatile uint8_t scancode = inb(PS2_DATA_PORT);
  keyboard_process(scancode);
  lapic_eoi();
}

bool ps2_get_key_event(keyboard_event_t* event) {
  if (dequeue(event)) {
    return true;
  }
  return false;
}