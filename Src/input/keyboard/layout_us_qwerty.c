#include <stdint.h>

#include <input/keyboard/keycode.h>

// A 256-element array, with 2 characters per element (Normal, Shifted)
const char layout_us_qwerty[256][2] = {
    // ROW 1: Numbers and Symbols
    [KEY_GRAVE] = {'`', '~'},
    [KEY_1] = {'1', '!'},
    [KEY_2] = {'2', '@'},
    [KEY_3] = {'3', '#'},
    [KEY_4] = {'4', '$'},
    [KEY_5] = {'5', '%'},
    [KEY_6] = {'6', '^'},
    [KEY_7] = {'7', '&'},
    [KEY_8] = {'8', '*'},
    [KEY_9] = {'9', '('},
    [KEY_0] = {'0', ')'},
    [KEY_MINUS] = {'-', '_'},
    [KEY_EQUAL] = {'=', '+'},
    [KEY_BACKSPACE] = {'\b', '\b'},  // Backspace control character

    // ROW 2: QWERTY Row
    [KEY_TAB] = {'\t', '\t'},  // Tab control character
    [KEY_Q] = {'q', 'Q'},
    [KEY_W] = {'w', 'W'},
    [KEY_E] = {'e', 'E'},
    [KEY_R] = {'r', 'R'},
    [KEY_T] = {'t', 'T'},
    [KEY_Y] = {'y', 'Y'},
    [KEY_U] = {'u', 'U'},
    [KEY_I] = {'i', 'I'},
    [KEY_O] = {'o', 'O'},
    [KEY_P] = {'p', 'P'},
    [KEY_LBRACKET] = {'[', '{'},
    [KEY_RBRACKET] = {']', '}'},
    [KEY_BACKSLASH] = {'\\', '|'},

    // ROW 3: ASDF Row
    [KEY_A] = {'a', 'A'},
    [KEY_S] = {'s', 'S'},
    [KEY_D] = {'d', 'D'},
    [KEY_F] = {'f', 'F'},
    [KEY_G] = {'g', 'G'},
    [KEY_H] = {'h', 'H'},
    [KEY_J] = {'j', 'J'},
    [KEY_K] = {'k', 'K'},
    [KEY_L] = {'l', 'L'},
    [KEY_SEMICOLON] = {';', ':'},
    [KEY_QUOTE] = {'\'', '\"'},
    [KEY_ENTER] = {'\n', '\n'},  // Newline control character

    // ROW 4: ZXCV Row
    [KEY_Z] = {'z', 'Z'},
    [KEY_X] = {'x', 'X'},
    [KEY_C] = {'c', 'C'},
    [KEY_V] = {'v', 'V'},
    [KEY_B] = {'b', 'B'},
    [KEY_N] = {'n', 'N'},
    [KEY_M] = {'m', 'M'},
    [KEY_COMMA] = {',', '<'},
    [KEY_PERIOD] = {'.', '>'},
    [KEY_SLASH] = {'/', '?'},

    // ROW 5: Spacebar
    [KEY_SPACE] = {' ', ' '},

    // NUMPAD
    [KEY_NUM_SLASH] = {'/', '/'},
    [KEY_NUM_STAR] = {'*', '*'},
    [KEY_NUM_MINUS] = {'-', '-'},
    [KEY_NUM_PLUS] = {'+', '+'},
    [KEY_NUM_ENTER] = {'\n', '\n'},
    [KEY_NUM_PERIOD] = {'.', '.'},
    [KEY_NUM_0] = {'0', '0'},
    [KEY_NUM_1] = {'1', '1'},
    [KEY_NUM_2] = {'2', '2'},
    [KEY_NUM_3] = {'3', '3'},
    [KEY_NUM_4] = {'4', '4'},
    [KEY_NUM_5] = {'5', '5'},
    [KEY_NUM_6] = {'6', '6'},
    [KEY_NUM_7] = {'7', '7'},
    [KEY_NUM_8] = {'8', '8'},
    [KEY_NUM_9] = {'9', '9'},
};
