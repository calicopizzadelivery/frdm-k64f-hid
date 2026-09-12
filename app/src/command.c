/*
 * ASCII line protocol. Every command answers exactly one line beginning OK or
 * ERR, matching the convention used by the relay controller on this bench so
 * host tooling can treat both the same way.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "command.h"
#include "hid_io.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>

#define FW_NAME    "k64f-hid-injector"
#define FW_VERSION "0.1.0"

#define LINE_MAX   160
#define TAP_MS     12      /* how long a tapped key or click is held */

struct keyname {
	const char *name;
	uint8_t usage;
};

/* Non-printing and navigation keys, by name. */
static const struct keyname named_keys[] = {
	{ "ENTER", 0x28 }, { "RETURN", 0x28 }, { "ESC", 0x29 },
	{ "BACKSPACE", 0x2A }, { "BKSP", 0x2A }, { "TAB", 0x2B },
	{ "SPACE", 0x2C }, { "CAPSLOCK", 0x39 },
	{ "F1", 0x3A }, { "F2", 0x3B }, { "F3", 0x3C }, { "F4", 0x3D },
	{ "F5", 0x3E }, { "F6", 0x3F }, { "F7", 0x40 }, { "F8", 0x41 },
	{ "F9", 0x42 }, { "F10", 0x43 }, { "F11", 0x44 }, { "F12", 0x45 },
	{ "PRTSCR", 0x46 }, { "SCROLLLOCK", 0x47 }, { "PAUSE", 0x48 },
	{ "INSERT", 0x49 }, { "HOME", 0x4A }, { "PGUP", 0x4B },
	{ "DELETE", 0x4C }, { "DEL", 0x4C }, { "END", 0x4D }, { "PGDN", 0x4E },
	{ "RIGHT", 0x4F }, { "LEFT", 0x50 }, { "DOWN", 0x51 }, { "UP", 0x52 },
};

/* Printable ASCII 0x20..0x7E -> usage code, and whether shift is needed. */
static bool ascii_to_usage(char c, uint8_t *usage, bool *shift)
{
	*shift = false;

	if (c >= 'a' && c <= 'z') {
		*usage = 0x04 + (c - 'a');
		return true;
	}
	if (c >= 'A' && c <= 'Z') {
		*usage = 0x04 + (c - 'A');
		*shift = true;
		return true;
	}
	if (c >= '1' && c <= '9') {
		*usage = 0x1E + (c - '1');
		return true;
	}

	switch (c) {
	case '0':  *usage = 0x27; return true;
	case ' ':  *usage = 0x2C; return true;
	case '-':  *usage = 0x2D; return true;
	case '=':  *usage = 0x2E; return true;
	case '[':  *usage = 0x2F; return true;
	case ']':  *usage = 0x30; return true;
	case '\\': *usage = 0x31; return true;
	case ';':  *usage = 0x33; return true;
	case '\'': *usage = 0x34; return true;
	case '`':  *usage = 0x35; return true;
	case ',':  *usage = 0x36; return true;
	case '.':  *usage = 0x37; return true;
	case '/':  *usage = 0x38; return true;
	/* Shifted forms of the above. */
	case '!':  *usage = 0x1E; *shift = true; return true;
	case '@':  *usage = 0x1F; *shift = true; return true;
	case '#':  *usage = 0x20; *shift = true; return true;
	case '$':  *usage = 0x21; *shift = true; return true;
	case '%':  *usage = 0x22; *shift = true; return true;
	case '^':  *usage = 0x23; *shift = true; return true;
	case '&':  *usage = 0x24; *shift = true; return true;
	case '*':  *usage = 0x25; *shift = true; return true;
	case '(':  *usage = 0x26; *shift = true; return true;
	case ')':  *usage = 0x27; *shift = true; return true;
	case '_':  *usage = 0x2D; *shift = true; return true;
	case '+':  *usage = 0x2E; *shift = true; return true;
	case '{':  *usage = 0x2F; *shift = true; return true;
	case '}':  *usage = 0x30; *shift = true; return true;
	case '|':  *usage = 0x31; *shift = true; return true;
	case ':':  *usage = 0x33; *shift = true; return true;
	case '"':  *usage = 0x34; *shift = true; return true;
	case '~':  *usage = 0x35; *shift = true; return true;
	case '<':  *usage = 0x36; *shift = true; return true;
	case '>':  *usage = 0x37; *shift = true; return true;
	case '?':  *usage = 0x38; *shift = true; return true;
	default:   return false;
	}
}

static bool name_to_modifier(const char *tok, uint8_t *mod)
{
	if (!strcmp(tok, "CTRL") || !strcmp(tok, "CONTROL")) {
		*mod = MOD_LCTRL;
	} else if (!strcmp(tok, "SHIFT")) {
		*mod = MOD_LSHIFT;
	} else if (!strcmp(tok, "ALT")) {
		*mod = MOD_LALT;
	} else if (!strcmp(tok, "GUI") || !strcmp(tok, "WIN") ||
		   !strcmp(tok, "META") || !strcmp(tok, "SUPER")) {
		*mod = MOD_LGUI;
	} else {
		return false;
	}

	return true;
}

static bool name_to_usage(const char *tok, uint8_t *usage)
{
	for (size_t i = 0; i < ARRAY_SIZE(named_keys); i++) {
		if (!strcmp(tok, named_keys[i].name)) {
			*usage = named_keys[i].usage;
			return true;
		}
	}

	/* A single printable character is also a valid key name. */
	if (strlen(tok) == 1) {
		bool shift;

		return ascii_to_usage(tok[0], usage, &shift);
	}

	return false;
}

/*
 * Parse "ctrl+alt+t" into a modifier mask and at most one ordinary key.
 * Returns false if any element is unrecognised.
 */
static bool parse_combo(char *spec, uint8_t *mod, uint8_t *usage)
{
	char *save = NULL;
	char *tok;

	*mod = 0U;
	*usage = 0U;

	for (tok = strtok_r(spec, "+", &save); tok != NULL;
	     tok = strtok_r(NULL, "+", &save)) {
		uint8_t m, u;

		if (name_to_modifier(tok, &m)) {
			*mod |= m;
		} else if (name_to_usage(tok, &u)) {
			if (*usage != 0U) {
				return false;   /* more than one ordinary key */
			}
			*usage = u;
		} else {
			return false;
		}
	}

	return true;
}

static bool parse_button(const char *tok, uint8_t *mask)
{
	if (!strcmp(tok, "LEFT") || !strcmp(tok, "L")) {
		*mask = MOUSE_LEFT;
	} else if (!strcmp(tok, "RIGHT") || !strcmp(tok, "R")) {
		*mask = MOUSE_RIGHT;
	} else if (!strcmp(tok, "MIDDLE") || !strcmp(tok, "M")) {
		*mask = MOUSE_MIDDLE;
	} else {
		return false;
	}

	return true;
}

static void emit(cmd_reply_fn reply, void *ctx, const char *fmt, ...)
{
	char buf[LINE_MAX];
	va_list ap;

	va_start(ap, fmt);
	vsnprintk(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	reply(ctx, buf);
}

static void report_result(cmd_reply_fn reply, void *ctx, int ret,
			  const char *what)
{
	if (ret == 0) {
		emit(reply, ctx, "OK %s", what);
	} else if (ret == -EAGAIN) {
		emit(reply, ctx, "ERR USB NOT READY (host has not configured "
				 "the HID interfaces)");
	} else if (ret == -ENOSPC) {
		emit(reply, ctx, "ERR TOO MANY KEYS HELD (max %d)", KBD_MAX_KEYS);
	} else {
		emit(reply, ctx, "ERR %s FAILED %d", what, ret);
	}
}

static void print_help(cmd_reply_fn reply, void *ctx)
{
	reply(ctx, "commands (verbs case-insensitive, one per line):");
	reply(ctx, "  TYPE <text>         type the rest of the line literally");
	reply(ctx, "  KEY <combo>         press and release, e.g. KEY ctrl+alt+t");
	reply(ctx, "  KEYDOWN <combo>     press and hold");
	reply(ctx, "  KEYUP <combo>       release");
	reply(ctx, "  RELEASE             release every held key and button");
	reply(ctx, "  MOUSE MOVE <dx> <dy>    relative, -127..127 per report");
	reply(ctx, "  MOUSE SCROLL <n>        wheel clicks, -127..127");
	reply(ctx, "  MOUSE CLICK <button>    left | right | middle");
	reply(ctx, "  MOUSE DOWN|UP <button>");
	reply(ctx, "  STATE               held modifiers and buttons");
	reply(ctx, "  INFO                firmware, board, USB state, serial");
	reply(ctx, "  VERSION             firmware name and version");
	reply(ctx, "  HELP                this text");
	reply(ctx, "key names: a-z 0-9, punctuation, enter esc tab space backspace");
	reply(ctx, "           del home end pgup pgdn up down left right f1-f12");
	reply(ctx, "modifiers: ctrl shift alt gui (aliases: control win meta super)");
}

static void print_info(cmd_reply_fn reply, void *ctx)
{
	uint8_t id[12];
	char hex[sizeof(id) * 2 + 1];
	ssize_t len;

	len = hwinfo_get_device_id(id, sizeof(id));
	if (len <= 0) {
		strcpy(hex, "unknown");
	} else {
		for (ssize_t i = 0; i < len; i++) {
			snprintk(&hex[i * 2], 3, "%02X", id[i]);
		}
	}

	emit(reply, ctx, "OK INFO fw=%s ver=%s board=%s usb=%s serial=%s",
	     FW_NAME, FW_VERSION, CONFIG_BOARD,
	     hid_io_ready() ? "ready" : "down", hex);
}

void command_execute(const char *raw, cmd_reply_fn reply, void *ctx)
{
	char work[LINE_MAX];        /* uppercased, tokenised */
	char original[LINE_MAX];    /* untouched, for TYPE */
	char *tok[5];
	int n = 0;

	if (raw == NULL) {
		return;
	}

	strncpy(original, raw, sizeof(original) - 1);
	original[sizeof(original) - 1] = '\0';
	strncpy(work, raw, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';

	for (char *p = work; *p != '\0'; p++) {
		*p = (char)toupper((unsigned char)*p);
	}

	/* TYPE is handled before tokenising, since its argument is the rest of
	   the line verbatim -- spaces, case and all. */
	if (!strncmp(work, "TYPE ", 5)) {
		const char *text = original + 5;
		int ret = 0;

		for (const char *c = text; *c != '\0' && ret == 0; c++) {
			uint8_t usage;
			bool shift;

			if (!ascii_to_usage(*c, &usage, &shift)) {
				emit(reply, ctx,
				     "ERR UNTYPEABLE CHARACTER 0x%02X", *c);
				return;
			}
			ret = hid_kbd_tap(shift ? MOD_LSHIFT : 0U, usage, TAP_MS);
		}
		report_result(reply, ctx, ret, "TYPE");
		return;
	}

	{
		char *save = NULL;

		for (char *t = strtok_r(work, " \t", &save);
		     t != NULL && n < (int)ARRAY_SIZE(tok);
		     t = strtok_r(NULL, " \t", &save)) {
			tok[n++] = t;
		}
	}

	if (n == 0) {
		return;                 /* blank line: stay quiet */
	}

	if (!strcmp(tok[0], "HELP") || !strcmp(tok[0], "?")) {
		print_help(reply, ctx);
		return;
	}
	if (!strcmp(tok[0], "VERSION")) {
		emit(reply, ctx, "%s %s", FW_NAME, FW_VERSION);
		return;
	}
	if (!strcmp(tok[0], "INFO")) {
		print_info(reply, ctx);
		return;
	}
	if (!strcmp(tok[0], "STATE")) {
		emit(reply, ctx, "STATE mods=0x%02X buttons=0x%02X usb=%s",
		     hid_kbd_modifiers(), hid_mouse_button_state(),
		     hid_io_ready() ? "ready" : "down");
		return;
	}
	if (!strcmp(tok[0], "RELEASE")) {
		int ret = hid_kbd_release_all();

		if (ret == 0) {
			ret = hid_mouse_buttons(0U);
		}
		report_result(reply, ctx, ret, "RELEASE");
		return;
	}

	if (!strcmp(tok[0], "KEY") || !strcmp(tok[0], "KEYDOWN") ||
	    !strcmp(tok[0], "KEYUP")) {
		uint8_t mod, usage;
		int ret;

		if (n < 2) {
			emit(reply, ctx, "ERR %s NEEDS A KEY", tok[0]);
			return;
		}
		if (!parse_combo(tok[1], &mod, &usage)) {
			emit(reply, ctx, "ERR BAD KEY COMBO");
			return;
		}

		if (!strcmp(tok[0], "KEYDOWN")) {
			ret = hid_kbd_press(mod, usage);
		} else if (!strcmp(tok[0], "KEYUP")) {
			ret = hid_kbd_release(mod, usage);
		} else {
			ret = hid_kbd_tap(mod, usage, TAP_MS);
		}
		report_result(reply, ctx, ret, tok[0]);
		return;
	}

	if (!strcmp(tok[0], "MOUSE")) {
		if (n < 2) {
			emit(reply, ctx, "ERR MOUSE NEEDS A SUBCOMMAND");
			return;
		}

		if (!strcmp(tok[1], "MOVE")) {
			if (n < 4) {
				emit(reply, ctx, "ERR MOUSE MOVE NEEDS DX AND DY");
				return;
			}
			report_result(reply, ctx,
				      hid_mouse_move(atoi(tok[2]), atoi(tok[3]), 0),
				      "MOUSE MOVE");
			return;
		}
		if (!strcmp(tok[1], "SCROLL")) {
			if (n < 3) {
				emit(reply, ctx, "ERR MOUSE SCROLL NEEDS AN AMOUNT");
				return;
			}
			report_result(reply, ctx, hid_mouse_move(0, 0, atoi(tok[2])),
				      "MOUSE SCROLL");
			return;
		}

		if (!strcmp(tok[1], "CLICK") || !strcmp(tok[1], "DOWN") ||
		    !strcmp(tok[1], "UP")) {
			uint8_t mask;
			int ret;

			if (n < 3) {
				emit(reply, ctx, "ERR MOUSE %s NEEDS A BUTTON",
				     tok[1]);
				return;
			}
			if (!parse_button(tok[2], &mask)) {
				emit(reply, ctx, "ERR BAD BUTTON %s", tok[2]);
				return;
			}

			if (!strcmp(tok[1], "CLICK")) {
				ret = hid_mouse_click(mask, TAP_MS);
			} else if (!strcmp(tok[1], "DOWN")) {
				ret = hid_mouse_buttons(
					hid_mouse_button_state() | mask);
			} else {
				ret = hid_mouse_buttons(
					hid_mouse_button_state() & (uint8_t)~mask);
			}
			report_result(reply, ctx, ret, "MOUSE");
			return;
		}

		emit(reply, ctx, "ERR UNKNOWN MOUSE SUBCOMMAND %s", tok[1]);
		return;
	}

	emit(reply, ctx, "ERR UNKNOWN COMMAND %s", tok[0]);
}
