#include <drivers/keyboard/ps2/keyboard.h>
#include <input/keyboard/keyboard.h>
#include <libs/string.h>
#include <process/locks.h>
#include <process/scheduler.h>
#include <process/waitqueue.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <tty/tty.h>
#include <utils/log.h>

extern const char layout_us_qwerty[256][2];

struct kbd_state_s kbd_state;

void keyboard_init(void) {
  memset(&kbd_state.queue, 0, sizeof(kbd_state.queue));
  waitqueue_init(&kbd_state.waitqueue);
  mutex_init(&kbd_state.lock);
  ps2_init();
}

// Producer
static bool queue_enqueue(key_queue_t* q, uint32_t unicode, uint8_t keycode,
                          uint16_t modifiers, bool is_pressed, bool is_repeat) {
  size_t current_tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
  size_t current_head = atomic_load_explicit(&q->head, memory_order_acquire);

  if ((current_tail - current_head) >= KBD_QUEUE_SIZE) {
    return false;
  }

  q->buffer[current_tail & (KBD_QUEUE_SIZE - 1)] =
      (kbd_key_event_t){.unicode = unicode,
                        .keycode = keycode,
                        .modifiers = modifiers,
                        .is_pressed = is_pressed,
                        .is_repeat = is_repeat};

  atomic_store_explicit(&q->tail, current_tail + 1, memory_order_release);
  return true;
}

// Consumer
static bool queue_dequeue(key_queue_t* q, kbd_key_event_t* out_keycode) {
  size_t current_head = atomic_load_explicit(&q->head, memory_order_relaxed);
  size_t current_tail = atomic_load_explicit(&q->tail, memory_order_acquire);

  // Check if queue is empty
  if (current_head == current_tail) {
    return false;
  }

  kbd_key_event_t* key_event = &q->buffer[current_head & (KBD_QUEUE_SIZE - 1)];
  *out_keycode = *key_event;

  atomic_store_explicit(&q->head, current_head + 1, memory_order_release);
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
                                  bool is_pressed, bool is_repeat) {
  const uint32_t ch = (uint32_t)translate_keycode(keycode, modifiers);

  queue_enqueue(&kbd_state.queue, ch, keycode, modifiers, is_pressed,
                is_repeat);
  waitqueue_wake(&kbd_state.waitqueue);
}

bool kbd_dequeue_key_event(kbd_key_event_t* event) {
  if (!queue_dequeue(&kbd_state.queue, event)) {
    waitqueue_wait(&kbd_state.waitqueue);
    return false;
  }

  return true;
}
