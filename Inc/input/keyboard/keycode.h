#pragma once

#ifndef _OS_KEYCODE_H
#define _OS_KEYCODE_H

#include <stdbool.h>
#include <stdint.h>

// The Bit-Packing Macro:
// Top 3 bits = Row (0 to 7)
// Bottom 5 bits = Column (0 to 31)
#define MAKE_KEYCODE(row, col) (((row & 0x07) << 5) | (col & 0x1F))

// ROW 0: Function Keys and System Keys
#define KEY_ESC MAKE_KEYCODE(0, 0)
#define KEY_F1 MAKE_KEYCODE(0, 1)
#define KEY_F2 MAKE_KEYCODE(0, 2)
#define KEY_F3 MAKE_KEYCODE(0, 3)
#define KEY_F4 MAKE_KEYCODE(0, 4)
#define KEY_F5 MAKE_KEYCODE(0, 5)
#define KEY_F6 MAKE_KEYCODE(0, 6)
#define KEY_F7 MAKE_KEYCODE(0, 7)
#define KEY_F8 MAKE_KEYCODE(0, 8)
#define KEY_F9 MAKE_KEYCODE(0, 9)
#define KEY_F10 MAKE_KEYCODE(0, 10)
#define KEY_F11 MAKE_KEYCODE(0, 11)
#define KEY_F12 MAKE_KEYCODE(0, 12)
#define KEY_PRINTSCREEN MAKE_KEYCODE(0, 13)
#define KEY_SCROLLLOCK MAKE_KEYCODE(0, 14)
#define KEY_PAUSE MAKE_KEYCODE(0, 15)

// ROW 1: Numbers, Nav Cluster Top, and Numpad Top
#define KEY_GRAVE MAKE_KEYCODE(1, 0)  // ` or ~
#define KEY_1 MAKE_KEYCODE(1, 1)
#define KEY_2 MAKE_KEYCODE(1, 2)
#define KEY_3 MAKE_KEYCODE(1, 3)
#define KEY_4 MAKE_KEYCODE(1, 4)
#define KEY_5 MAKE_KEYCODE(1, 5)
#define KEY_6 MAKE_KEYCODE(1, 6)
#define KEY_7 MAKE_KEYCODE(1, 7)
#define KEY_8 MAKE_KEYCODE(1, 8)
#define KEY_9 MAKE_KEYCODE(1, 9)
#define KEY_0 MAKE_KEYCODE(1, 10)
#define KEY_MINUS MAKE_KEYCODE(1, 11)  // - or _
#define KEY_EQUAL MAKE_KEYCODE(1, 12)  // = or +
#define KEY_BACKSPACE MAKE_KEYCODE(1, 13)
#define KEY_INSERT MAKE_KEYCODE(1, 14)
#define KEY_HOME MAKE_KEYCODE(1, 15)
#define KEY_PAGEUP MAKE_KEYCODE(1, 16)
#define KEY_NUM_LOCK MAKE_KEYCODE(1, 17)
#define KEY_NUM_SLASH MAKE_KEYCODE(1, 18)  // Numpad /
#define KEY_NUM_STAR MAKE_KEYCODE(1, 19)   // Numpad *
#define KEY_NUM_MINUS MAKE_KEYCODE(1, 20)  // Numpad -

// ROW 2: QWERTY Row, Nav Cluster Middle, and Numpad High
#define KEY_TAB MAKE_KEYCODE(2, 0)
#define KEY_Q MAKE_KEYCODE(2, 1)
#define KEY_W MAKE_KEYCODE(2, 2)
#define KEY_E MAKE_KEYCODE(2, 3)
#define KEY_R MAKE_KEYCODE(2, 4)
#define KEY_T MAKE_KEYCODE(2, 5)
#define KEY_Y MAKE_KEYCODE(2, 6)
#define KEY_U MAKE_KEYCODE(2, 7)
#define KEY_I MAKE_KEYCODE(2, 8)
#define KEY_O MAKE_KEYCODE(2, 9)
#define KEY_P MAKE_KEYCODE(2, 10)
#define KEY_LBRACKET MAKE_KEYCODE(2, 11)   // [ or {
#define KEY_RBRACKET MAKE_KEYCODE(2, 12)   // ] or }
#define KEY_BACKSLASH MAKE_KEYCODE(2, 13)  // \ or |
#define KEY_DELETE MAKE_KEYCODE(2, 14)
#define KEY_END MAKE_KEYCODE(2, 15)
#define KEY_PAGEDOWN MAKE_KEYCODE(2, 16)
#define KEY_NUM_7 MAKE_KEYCODE(2, 17)
#define KEY_NUM_8 MAKE_KEYCODE(2, 18)
#define KEY_NUM_9 MAKE_KEYCODE(2, 19)
#define KEY_NUM_PLUS MAKE_KEYCODE(2, 20)  // Numpad +

// ROW 3: ASDF Row, and Numpad Middle
#define KEY_CAPSLOCK MAKE_KEYCODE(3, 0)
#define KEY_A MAKE_KEYCODE(3, 1)
#define KEY_S MAKE_KEYCODE(3, 2)
#define KEY_D MAKE_KEYCODE(3, 3)
#define KEY_F MAKE_KEYCODE(3, 4)
#define KEY_G MAKE_KEYCODE(3, 5)
#define KEY_H MAKE_KEYCODE(3, 6)
#define KEY_J MAKE_KEYCODE(3, 7)
#define KEY_K MAKE_KEYCODE(3, 8)
#define KEY_L MAKE_KEYCODE(3, 9)
#define KEY_SEMICOLON MAKE_KEYCODE(3, 10)  // ; or :
#define KEY_QUOTE MAKE_KEYCODE(3, 11)      // ' or "
#define KEY_ENTER MAKE_KEYCODE(3, 12)      // Return
#define KEY_NUM_4 MAKE_KEYCODE(3, 13)
#define KEY_NUM_5 MAKE_KEYCODE(3, 14)
#define KEY_NUM_6 MAKE_KEYCODE(3, 15)

// ROW 4: ZXCV Row, Arrows (Up), and Numpad Low
#define KEY_LSHIFT MAKE_KEYCODE(4, 0)
#define KEY_Z MAKE_KEYCODE(4, 1)
#define KEY_X MAKE_KEYCODE(4, 2)
#define KEY_C MAKE_KEYCODE(4, 3)
#define KEY_V MAKE_KEYCODE(4, 4)
#define KEY_B MAKE_KEYCODE(4, 5)
#define KEY_N MAKE_KEYCODE(4, 6)
#define KEY_M MAKE_KEYCODE(4, 7)
#define KEY_COMMA MAKE_KEYCODE(4, 8)   // , or <
#define KEY_PERIOD MAKE_KEYCODE(4, 9)  // . or >
#define KEY_SLASH MAKE_KEYCODE(4, 10)  // / or ?
#define KEY_RSHIFT MAKE_KEYCODE(4, 11)
#define KEY_UP MAKE_KEYCODE(4, 12)  // Up Arrow
#define KEY_NUM_1 MAKE_KEYCODE(4, 13)
#define KEY_NUM_2 MAKE_KEYCODE(4, 14)
#define KEY_NUM_3 MAKE_KEYCODE(4, 15)
#define KEY_NUM_ENTER MAKE_KEYCODE(4, 16)

// ROW 5: Bottom Modifiers, Space, Arrows (Left/Down/Right)
#define KEY_LCTRL MAKE_KEYCODE(5, 0)
#define KEY_LSUPER MAKE_KEYCODE(5, 1)  // Left Windows/Command key
#define KEY_LALT MAKE_KEYCODE(5, 2)
#define KEY_SPACE MAKE_KEYCODE(5, 3)
#define KEY_RALT MAKE_KEYCODE(5, 4)
#define KEY_RSUPER MAKE_KEYCODE(5, 5)  // Right Windows/Command key
#define KEY_MENU MAKE_KEYCODE(5, 6)    // Application/Menu key
#define KEY_RCTRL MAKE_KEYCODE(5, 7)
#define KEY_LEFT MAKE_KEYCODE(5, 8)    // Left Arrow
#define KEY_DOWN MAKE_KEYCODE(5, 9)    // Down Arrow
#define KEY_RIGHT MAKE_KEYCODE(5, 10)  // Right Arrow
#define KEY_NUM_0 MAKE_KEYCODE(5, 11)
#define KEY_NUM_PERIOD MAKE_KEYCODE(5, 12)  // Numpad .

// ROW 6: Unmapped / Multimedia / ACPI Keys (Optional)
#define KEY_VOLUME_UP MAKE_KEYCODE(6, 0)
#define KEY_VOLUME_DOWN MAKE_KEYCODE(6, 1)
#define KEY_VOLUME_MUTE MAKE_KEYCODE(6, 2)
#define KEY_POWER MAKE_KEYCODE(6, 3)

#endif  // _OS_KEYCODE_H