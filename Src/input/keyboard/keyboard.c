#include <drivers/keyboard/ps2/keyboard.h>
#include <input/keyboard/keyboard.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <process/waitqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <tty/tty.h>
#include <utils/log.h>

extern const char layout_us_qwerty[256][2];

#define BUFFER_SIZE 256

waitqueue_t kbd_waitqueue;
static spinlock_t kbd_buf_lock = {0};
static kbd_key_event_t kbd_key_events_buf[BUFFER_SIZE] = {0};
static size_t head = 0;
static size_t tail = 0;

void keyboard_init(void) {
  spinlock_init(&kbd_buf_lock);
  waitqueue_init(&kbd_waitqueue);
  ps2_init();
}

static inline bool is_queue_empty(void) { return head == tail; }

static inline bool is_queue_full(void) {
  return (tail + 1) % BUFFER_SIZE == head;
}

static inline bool enqueue(uint32_t unicode, uint8_t keycode,
                           uint16_t modifiers, bool is_pressed) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&kbd_buf_lock, flags);

  if (is_queue_full()) {
    SPIN_LOCK_RELEASE(&kbd_buf_lock, flags);
    return false;
  }

  kbd_key_events_buf[tail] = (kbd_key_event_t){.unicode = unicode,
                                               .keycode = keycode,
                                               .modifiers = modifiers,
                                               .is_pressed = is_pressed};

  tail = (tail + 1) % BUFFER_SIZE;
  SPIN_LOCK_RELEASE(&kbd_buf_lock, flags);
  return true;
}

static inline bool dequeue(kbd_key_event_t* event) {
  if (is_queue_empty()) {
    return false;
  }

  *event = kbd_key_events_buf[head];
  head = (head + 1) % BUFFER_SIZE;

  return true;
}

static inline char translate_keycode(uint8_t keycode, uint16_t modifiers) {
  char normal_char = layout_us_qwerty[keycode][0];
  char shift_char = layout_us_qwerty[keycode][1];

  if (normal_char == 0) return 0;

  bool use_shift =
      modifiers & KBD_MODIFIER_LSHIFT || modifiers & KBD_MODIFIER_RSHIFT;

  // Caps lock reverses the shift behavior, but ONLY for letters (a-z)
  if (modifiers & KBD_CAPSLOCK_ACTIVE && normal_char >= 'a' &&
      normal_char <= 'z') {
    use_shift = !use_shift;
  }

  if (use_shift) {
    return shift_char;
  }

  return normal_char;
}

void keyboard_process_key_handler(uint8_t keycode, uint16_t modifiers,
                                  bool is_pressed) {
  const uint32_t ch = (uint32_t)translate_keycode(keycode, modifiers);

  enqueue(ch, keycode, modifiers, is_pressed);
  waitqueue_wake(&kbd_waitqueue);
}

bool kbd_dqueue_key_event(kbd_key_event_t* event) {
  unsigned long flags;
  SPIN_LOCK_ACQUIRE(&kbd_buf_lock, flags);

  bool result = dequeue(event);

  SPIN_LOCK_RELEASE(&kbd_buf_lock, flags);
  return result;
}
