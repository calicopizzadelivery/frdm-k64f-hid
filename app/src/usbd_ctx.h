/* SPDX-License-Identifier: Apache-2.0 */
#ifndef K64F_HID_USBD_CTX_H_
#define K64F_HID_USBD_CTX_H_

#include <zephyr/usb/usbd.h>

/*
 * Placeholder identifiers. 0x2fe3 is the Zephyr project's vendor id, which is
 * fine on a private bench but must not be shipped: get a real VID/PID before
 * this leaves the lab.
 */
#define K64F_HID_VID 0x2fe3
#define K64F_HID_PID 0x0001

/* Build the device context and run usbd_init(). NULL on failure. */
struct usbd_context *k64f_usbd_init(void);

#endif /* K64F_HID_USBD_CTX_H_ */
