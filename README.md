# frdm-k64f-hid

Zephyr firmware turning an NXP FRDM-K64F into a USB keyboard and mouse that a
host can drive over a serial command protocol. Built to orchestrate the UI of a
Jetson under test, but there is nothing Jetson-specific in it.

Two USB ports, two jobs:

- the **K64F native USB port** goes to the target, which sees a plain HID
  keyboard and mouse
- the **OpenSDA port** goes to the development rig and carries the command
  console (and the debugger)

So the machine issuing the keystrokes is not the machine receiving them, which
is the whole point: the target cannot tell the difference between this and
someone at a keyboard.

## Commands

115200 baud on the OpenSDA CDC port, one ASCII command per line. Verbs are
case-insensitive and every command answers a single line starting `OK` or `ERR`,
matching the convention used by the relay controller on the same bench.

| Command | Effect |
|---|---|
| `TYPE <text>` | Type the rest of the line literally, case preserved |
| `KEY <combo>` | Press and release, e.g. `KEY ctrl+alt+t` |
| `KEYDOWN <combo>` / `KEYUP <combo>` | Press and hold / release |
| `RELEASE` | Release every held key and button |
| `MOUSE MOVE <dx> <dy>` | Relative move, −127..127 per report |
| `MOUSE SCROLL <n>` | Wheel clicks |
| `MOUSE CLICK\|DOWN\|UP <button>` | `left`, `right` or `middle` |
| `STATE` | Held modifiers, held buttons, USB state |
| `INFO` | Firmware, board, USB state, SoC serial |
| `VERSION`, `HELP` | |

Key names: `a`–`z`, `0`–`9`, punctuation, and `enter esc tab space backspace
del home end pgup pgdn up down left right f1`–`f12`.
Modifiers: `ctrl shift alt gui` (aliases `control win meta super`).

```
> INFO
OK INFO fw=k64f-hid-injector ver=0.1.0 board=frdm_k64f usb=ready serial=FFFFFFFF4E45805140040019
> KEY ctrl+alt+t
OK KEY
> TYPE uname -a
OK TYPE
```

`ERR USB NOT READY` means the target has not configured the HID interfaces —
the board is powered but nothing is listening, so commands are refused rather
than silently dropped.

## Host CLI

```bash
./tools/hidctl.py info
./tools/hidctl.py type "hello world"
./tools/hidctl.py key ctrl+alt+t
./tools/hidctl.py mouse move 40 30
./tools/hidctl.py console          # interactive
```

Needs `pyserial`. Exit status is non-zero when the firmware answers `ERR`, so it
composes in shell scripts. Note that Zephyr's console echoes what it receives;
`hidctl.py` strips the echo, so anything talking to the port directly should
expect the command back before the reply.

## Design notes

**Two HID interfaces, not one with report IDs.** Boot protocol is only defined
for a keyboard or a mouse individually, so splitting them keeps both usable by a
BIOS or bootloader that speaks only boot protocol, and keeps the reports free of
a leading report-ID byte.

**Three HID callbacks are mandatory.** `hid_device_register()` returns `-EINVAL`
without `get_report` (required for every HID device) and without `set_protocol`
(required for any interface declaring boot protocol, which both of these do).
`set_report` is additionally required for the keyboard, which declares an LED
output report. Missing any of them fails at registration, not at build time.

**Reports are submitted synchronously.** No `input_report_done` callback is
registered, so `hid_device_submit_report()` blocks until the report is on the
wire. For injection that ordering matters: a press must reach the target before
the matching release is queued.

The command layer is transport-agnostic — it takes a line and a reply function —
so the Ethernet port can grow a TCP listener sharing the same parser.

## Build and flash

```bash
./scripts/install-toolchain.sh   # west, Zephyr v4.4.2, ARM SDK -- no root
./scripts/build.sh
./scripts/flash.sh
```

The toolchain installer needs no apt: Ubuntu 24.04 ships python3 without pip and
without a working `ensurepip`, so it creates the venv `--without-pip` and
bootstraps pip into it by hand.

Flashing uses the OpenSDA mass-storage bootloader — dropping a raw `.bin` on the
`MBED` volume programs and resets the target, needing no permissions at all.
That volume unmounts across the reset, so a second flash in a row usually needs
it remounting; `flash.sh` handles that.

`scripts/host-setup.sh` is only needed for SWD work via pyocd (target `k64f`),
and adds a stable `/dev/frdm-k64f` symlink for the console.

## USB identifiers

The firmware advertises `2fe3:0001`. **0x2fe3 is the Zephyr project's vendor ID**
and is fine on a private bench but must not be shipped — get a real VID/PID
before this leaves the lab. Both are at the top of `app/src/usbd_ctx.h`.

## Status

Verified on hardware 2026-09-12 with both USB ports on the development machine:
enumerates as `2fe3:0001`, X sees it as both a slave pointer and a slave
keyboard, `MOUSE MOVE` moves the pointer, and `KEYDOWN ctrl` / `KEYUP ctrl`
raise and clear the modifier. Not yet exercised against the Jetson.

Not yet implemented: the Ethernet transport, and a writable device identity to
match the relay controller's `SETID` (which needs the settings/NVS subsystem).

## Layout

```
app/                     the Zephyr application
  src/main.c             init and the console loop
  src/usbd_ctx.c         USB device context
  src/hid_io.c           the two HID interfaces and report state
  src/command.c          ASCII protocol, keymap, parser
  app.overlay            the two zephyr,hid-device nodes
scripts/install-toolchain.sh
scripts/build.sh
scripts/flash.sh         OpenSDA mass-storage flash
scripts/host-setup.sh    one-time root: udev for SWD and a stable console name
tools/hidctl.py          host CLI
```

## Licence

Apache-2.0. See [LICENSE](LICENSE).
