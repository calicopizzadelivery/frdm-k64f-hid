/*
 * USB device context for the HID injector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usbd_ctx.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_ctx, LOG_LEVEL_WRN);

USBD_DEVICE_DEFINE(k64f_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   K64F_HID_VID, K64F_HID_PID);

USBD_DESC_LANG_DEFINE(k64f_lang);
USBD_DESC_MANUFACTURER_DEFINE(k64f_mfr, "jetson-bench");
USBD_DESC_PRODUCT_DEFINE(k64f_product, "FRDM-K64F HID injector");
USBD_DESC_SERIAL_NUMBER_DEFINE(k64f_sn);
USBD_DESC_CONFIG_DEFINE(k64f_fs_desc, "HID injector FS configuration");

/*
 * Bus powered, no remote wakeup, so bmAttributes contributes nothing. This has
 * to be a literal: USBD_CONFIGURATION_DEFINE builds a static initialiser, and
 * a const variable is not a constant expression there.
 */
#define K64F_CFG_ATTRIBUTES 0
#define K64F_CFG_MAX_POWER  250

USBD_CONFIGURATION_DEFINE(k64f_fs_config, K64F_CFG_ATTRIBUTES,
			  K64F_CFG_MAX_POWER, &k64f_fs_desc);

struct usbd_context *k64f_usbd_init(void)
{
	int err;

	err = usbd_add_descriptor(&k64f_usbd, &k64f_lang);
	if (err) {
		LOG_ERR("language descriptor: %d", err);
		return NULL;
	}

	err = usbd_add_descriptor(&k64f_usbd, &k64f_mfr);
	if (err) {
		LOG_ERR("manufacturer descriptor: %d", err);
		return NULL;
	}

	err = usbd_add_descriptor(&k64f_usbd, &k64f_product);
	if (err) {
		LOG_ERR("product descriptor: %d", err);
		return NULL;
	}

	err = usbd_add_descriptor(&k64f_usbd, &k64f_sn);
	if (err) {
		LOG_ERR("serial number descriptor: %d", err);
		return NULL;
	}

	err = usbd_add_configuration(&k64f_usbd, USBD_SPEED_FS, &k64f_fs_config);
	if (err) {
		LOG_ERR("add configuration: %d", err);
		return NULL;
	}

	err = usbd_register_all_classes(&k64f_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("register classes: %d", err);
		return NULL;
	}

	/* Take the class codes from the interface descriptors. */
	usbd_device_set_code_triple(&k64f_usbd, USBD_SPEED_FS, 0, 0, 0);

	err = usbd_init(&k64f_usbd);
	if (err) {
		LOG_ERR("usbd_init: %d", err);
		return NULL;
	}

	return &k64f_usbd;
}
