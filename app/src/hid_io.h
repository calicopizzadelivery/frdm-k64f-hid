/* SPDX-License-Identifier: Apache-2.0 */
#ifndef K64F_HID_IO_H_
#define K64F_HID_IO_H_

#include <stdbool.h>
#include <stdint.h>

/* Modifier bits, matching the HID keyboard report's first byte. */
#define MOD_LCTRL  0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT   0x04
#define MOD_LGUI   0x08

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

/* Up to six non-modifier keys can be held at once, per the boot report. */
#define KBD_MAX_KEYS 6

int  hid_io_init(void);
bool hid_io_ready(void);

/* All of these return 0, or a negative errno. */
int hid_kbd_press(uint8_t mod, uint8_t usage);
int hid_kbd_release(uint8_t mod, uint8_t usage);
int hid_kbd_release_all(void);
int hid_kbd_tap(uint8_t mod, uint8_t usage, uint32_t hold_ms);

int hid_mouse_move(int dx, int dy, int wheel);
int hid_mouse_buttons(uint8_t mask);
int hid_mouse_click(uint8_t mask, uint32_t hold_ms);

uint8_t hid_kbd_modifiers(void);
uint8_t hid_mouse_button_state(void);

#endif /* K64F_HID_IO_H_ */
