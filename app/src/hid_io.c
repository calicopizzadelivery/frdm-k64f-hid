/*
 * The two HID interfaces and the report state behind them.
 *
 * Reports are submitted synchronously: no input_report_done callback is
 * registered, so hid_device_submit_report() blocks until the report is on the
 * wire. That matters for injection, where a press must reach the host before
 * the matching release is queued.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hid_io.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/class/hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(hid_io, LOG_LEVEL_WRN);

static const uint8_t kbd_report_desc[] = HID_KEYBOARD_REPORT_DESC();
static const uint8_t mouse_report_desc[] = HID_MOUSE_REPORT_DESC(3);

static const struct device *kbd_dev;
static const struct device *mouse_dev;

static atomic_t kbd_ready;
static atomic_t mouse_ready;

/* [0] modifiers, [1] reserved, [2..7] key usages. Aligned: the HID API
   requires an aligned report buffer. */
static uint8_t __aligned(4) kbd_report[8];
/* [0] buttons, [1] dx, [2] dy, [3] wheel. */
static uint8_t __aligned(4) mouse_report[4];

static struct k_mutex lock;

/* Last LED state the host set, so Get Report can echo it back. */
static uint8_t kbd_leds;
/* Boot (0) or Report (1) protocol, as selected by the host. */
static uint8_t kbd_protocol = 1U;
static uint8_t mouse_protocol = 1U;

static void kbd_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	atomic_set(&kbd_ready, ready ? 1 : 0);
}

static void mouse_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	atomic_set(&mouse_ready, ready ? 1 : 0);
}

/*
 * The host sets keyboard LEDs (caps/num lock) through this. Nothing here
 * drives LEDs, but the request must be accepted or the host logs an error.
 */
static int kbd_set_report(const struct device *dev, const uint8_t type,
			  const uint8_t id, const uint16_t len,
			  const uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);

	if (type != HID_REPORT_TYPE_OUTPUT) {
		return -ENOTSUP;
	}
	if (len >= 1U && buf != NULL) {
		kbd_leds = buf[0];
	}

	return 0;
}

/*
 * Get Report is mandatory for every HID device, and Set Protocol is mandatory
 * for any interface that declares boot protocol -- which both of these do.
 * Omitting either makes hid_device_register() fail with -EINVAL.
 */
static int kbd_get_report(const struct device *dev, const uint8_t type,
			  const uint8_t id, const uint16_t len,
			  uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);

	if (type == HID_REPORT_TYPE_INPUT) {
		if (len < sizeof(kbd_report)) {
			return -ENOBUFS;
		}
		k_mutex_lock(&lock, K_FOREVER);
		memcpy(buf, kbd_report, sizeof(kbd_report));
		k_mutex_unlock(&lock);
		return (int)sizeof(kbd_report);
	}

	if (type == HID_REPORT_TYPE_OUTPUT) {
		if (len < 1U) {
			return -ENOBUFS;
		}
		buf[0] = kbd_leds;
		return 1;
	}

	return -ENOTSUP;
}

static int mouse_get_report(const struct device *dev, const uint8_t type,
			    const uint8_t id, const uint16_t len,
			    uint8_t *const buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);

	if (type != HID_REPORT_TYPE_INPUT) {
		return -ENOTSUP;
	}
	if (len < sizeof(mouse_report)) {
		return -ENOBUFS;
	}

	k_mutex_lock(&lock, K_FOREVER);
	memcpy(buf, mouse_report, sizeof(mouse_report));
	k_mutex_unlock(&lock);

	return (int)sizeof(mouse_report);
}

static void kbd_set_protocol(const struct device *dev, const uint8_t proto)
{
	ARG_UNUSED(dev);
	kbd_protocol = proto;
}

static void mouse_set_protocol(const struct device *dev, const uint8_t proto)
{
	ARG_UNUSED(dev);
	mouse_protocol = proto;
}

static const struct hid_device_ops kbd_ops = {
	.iface_ready = kbd_iface_ready,
	.get_report = kbd_get_report,
	.set_report = kbd_set_report,
	.set_protocol = kbd_set_protocol,
};

static const struct hid_device_ops mouse_ops = {
	.iface_ready = mouse_iface_ready,
	.get_report = mouse_get_report,
	.set_protocol = mouse_set_protocol,
};

bool hid_io_ready(void)
{
	return atomic_get(&kbd_ready) && atomic_get(&mouse_ready);
}

static int kbd_flush(void)
{
	if (!atomic_get(&kbd_ready)) {
		return -EAGAIN;
	}

	return hid_device_submit_report(kbd_dev, sizeof(kbd_report), kbd_report);
}

static int mouse_flush(void)
{
	if (!atomic_get(&mouse_ready)) {
		return -EAGAIN;
	}

	return hid_device_submit_report(mouse_dev, sizeof(mouse_report),
					mouse_report);
}

uint8_t hid_kbd_modifiers(void)
{
	return kbd_report[0];
}

uint8_t hid_mouse_button_state(void)
{
	return mouse_report[0];
}

int hid_kbd_press(uint8_t mod, uint8_t usage)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	kbd_report[0] |= mod;

	if (usage != 0U) {
		int slot = -1;

		for (int i = 2; i < 8; i++) {
			if (kbd_report[i] == usage) {
				slot = i;       /* already held */
				break;
			}
			if (kbd_report[i] == 0U && slot < 0) {
				slot = i;
			}
		}
		if (slot < 0) {
			k_mutex_unlock(&lock);
			return -ENOSPC;         /* six keys already held */
		}
		kbd_report[slot] = usage;
	}

	ret = kbd_flush();
	k_mutex_unlock(&lock);

	return ret;
}

int hid_kbd_release(uint8_t mod, uint8_t usage)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	kbd_report[0] &= (uint8_t)~mod;

	if (usage != 0U) {
		for (int i = 2; i < 8; i++) {
			if (kbd_report[i] == usage) {
				kbd_report[i] = 0U;
			}
		}
	}

	ret = kbd_flush();
	k_mutex_unlock(&lock);

	return ret;
}

int hid_kbd_release_all(void)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	memset(kbd_report, 0, sizeof(kbd_report));
	ret = kbd_flush();
	k_mutex_unlock(&lock);

	return ret;
}

int hid_kbd_tap(uint8_t mod, uint8_t usage, uint32_t hold_ms)
{
	int ret;

	ret = hid_kbd_press(mod, usage);
	if (ret) {
		return ret;
	}

	if (hold_ms != 0U) {
		k_msleep(hold_ms);
	}

	return hid_kbd_release(mod, usage);
}

static int8_t clamp_delta(int v)
{
	/* The report descriptor declares a signed byte per axis. */
	if (v > 127) {
		return 127;
	}
	if (v < -127) {
		return -127;
	}

	return (int8_t)v;
}

int hid_mouse_move(int dx, int dy, int wheel)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	mouse_report[1] = (uint8_t)clamp_delta(dx);
	mouse_report[2] = (uint8_t)clamp_delta(dy);
	mouse_report[3] = (uint8_t)clamp_delta(wheel);
	ret = mouse_flush();

	/* Movement is relative: clear it so the next report does not repeat it. */
	mouse_report[1] = 0U;
	mouse_report[2] = 0U;
	mouse_report[3] = 0U;
	k_mutex_unlock(&lock);

	return ret;
}

int hid_mouse_buttons(uint8_t mask)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	mouse_report[0] = mask;
	ret = mouse_flush();
	k_mutex_unlock(&lock);

	return ret;
}

int hid_mouse_click(uint8_t mask, uint32_t hold_ms)
{
	int ret;

	ret = hid_mouse_buttons(mouse_report[0] | mask);
	if (ret) {
		return ret;
	}

	if (hold_ms != 0U) {
		k_msleep(hold_ms);
	}

	return hid_mouse_buttons(mouse_report[0] & (uint8_t)~mask);
}

int hid_io_init(void)
{
	int ret;

	k_mutex_init(&lock);

	kbd_dev = DEVICE_DT_GET(DT_NODELABEL(hid_kbd));
	mouse_dev = DEVICE_DT_GET(DT_NODELABEL(hid_mouse));

	if (!device_is_ready(kbd_dev) || !device_is_ready(mouse_dev)) {
		LOG_ERR("HID devices not ready");
		return -ENODEV;
	}

	ret = hid_device_register(kbd_dev, kbd_report_desc,
				  sizeof(kbd_report_desc), &kbd_ops);
	if (ret) {
		LOG_ERR("keyboard register: %d", ret);
		return ret;
	}

	ret = hid_device_register(mouse_dev, mouse_report_desc,
				  sizeof(mouse_report_desc), &mouse_ops);
	if (ret) {
		LOG_ERR("mouse register: %d", ret);
		return ret;
	}

	return 0;
}
