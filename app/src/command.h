/* SPDX-License-Identifier: Apache-2.0 */
#ifndef K64F_HID_COMMAND_H_
#define K64F_HID_COMMAND_H_

/*
 * The command layer is deliberately transport-agnostic: it is handed a line
 * and a way to reply, so the same parser serves the OpenSDA serial console
 * today and a TCP listener on the Ethernet port later.
 */
typedef void (*cmd_reply_fn)(void *ctx, const char *line);

void command_execute(const char *raw, cmd_reply_fn reply, void *ctx);

#endif /* K64F_HID_COMMAND_H_ */
