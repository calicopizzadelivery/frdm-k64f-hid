/*
 * FRDM-K64F HID injector.
 *
 * The board's native USB port presents a keyboard and a mouse to the target
 * (the Jetson). Commands arrive on the OpenSDA CDC console and are turned into
 * HID reports. See command.c for the protocol, or send HELP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "command.h"
#include "hid_io.h"
#include "usbd_ctx.h"

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

static void serial_reply(void *ctx, const char *line)
{
	ARG_UNUSED(ctx);
	printk("%s\n", line);
}

int main(void)
{
	struct usbd_context *usbd;
	int ret;

	printk("boot: starting\n");

	/* HID classes must be registered before the device is initialised,
	   since usbd_register_all_classes() walks what is already there. */
	ret = hid_io_init();
	if (ret) {
		printk("ERR HID INIT FAILED %d\n", ret);
		return 0;
	}

	printk("boot: hid_io_init ok\n");

	usbd = k64f_usbd_init();
	if (usbd == NULL) {
		printk("ERR USB INIT FAILED\n");
		return 0;
	}

	printk("boot: usbd_init ok\n");

	ret = usbd_enable(usbd);
	if (ret) {
		printk("ERR USB ENABLE FAILED %d\n", ret);
		return 0;
	}

	printk("boot: usbd_enable ok\n");

	console_getline_init();

	printk("k64f-hid-injector ready, send HELP\n");

	while (true) {
		char *line = console_getline();

		if (line != NULL) {
			command_execute(line, serial_reply, NULL);
		}
	}

	return 0;
}
