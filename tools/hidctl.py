#!/usr/bin/env python3
"""Drive the FRDM-K64F HID injector from the host.

    hidctl.py info
    hidctl.py type "hello world"
    hidctl.py key ctrl+alt+t
    hidctl.py keydown shift        hidctl.py keyup shift
    hidctl.py mouse move 40 30
    hidctl.py mouse click left
    hidctl.py mouse scroll -3
    hidctl.py release
    hidctl.py console              # interactive
    hidctl.py raw "KEY f5"

Talks to the board's OpenSDA CDC port. The keyboard and mouse it presents come
out of the board's *other* USB port, which is the one wired to the target.

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import os
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("error: pyserial missing. install with: pip install pyserial")

BAUD = 115200
PORT_GLOBS = ("/dev/frdm-k64f", "/dev/serial/by-id/usb-MBED_MBED_CMSIS-DAP*")


def find_port() -> str:
    for pattern in PORT_GLOBS:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    sys.exit("error: no FRDM-K64F console found. Is the OpenSDA port connected? "
             "For a stable /dev/frdm-k64f, run scripts/host-setup.sh.")


def send(sp: "serial.Serial", line: str, settle: float = 1.2) -> list:
    """Send one command and return its reply lines.

    Zephyr's console echoes what it receives, so the echo of our own command
    comes back first and has to be dropped or every reply looks duplicated.
    """
    sp.reset_input_buffer()
    sp.write((line + "\r\n").encode("ascii", "replace"))
    sp.flush()

    deadline = time.monotonic() + settle
    raw = b""
    while time.monotonic() < deadline:
        chunk = sp.read(4096)
        if chunk:
            raw += chunk
            deadline = time.monotonic() + 0.25

    out = []
    for text in raw.decode("utf-8", "replace").splitlines():
        text = text.strip()
        if text and text != line:
            out.append(text)
    return out


def build_command(args) -> str:
    verb = args.verb.lower()

    if verb in ("info", "state", "version", "help", "release"):
        return verb.upper()
    if verb == "raw":
        if not args.rest:
            sys.exit("error: raw needs a line to send")
        return " ".join(args.rest)
    if verb == "type":
        if not args.rest:
            sys.exit("error: type needs text")
        # Joined with spaces so both quoted and bare arguments work.
        return "TYPE " + " ".join(args.rest)
    if verb in ("key", "keydown", "keyup"):
        if len(args.rest) != 1:
            sys.exit(f"error: {verb} takes one key or combo, e.g. ctrl+alt+t")
        return f"{verb.upper()} {args.rest[0]}"
    if verb == "mouse":
        if not args.rest:
            sys.exit("error: mouse needs move/click/scroll/down/up")
        return "MOUSE " + " ".join(args.rest)

    sys.exit(f"error: unknown command {args.verb!r}")


def cmd_console(sp) -> int:
    print("connected. type HELP, or ctrl-d to quit.")
    for line in send(sp, "INFO"):
        print(line)
    while True:
        try:
            line = input("hid> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if line.lower() in ("quit", "exit"):
            return 0
        if not line:
            continue
        for reply in send(sp, line):
            print(reply)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (default: autodetect)")
    ap.add_argument("-t", "--timeout", type=float, default=1.5)
    ap.add_argument("verb")
    ap.add_argument("rest", nargs="*")
    args = ap.parse_args()

    port = args.port or find_port()
    try:
        sp = serial.Serial(port, BAUD, timeout=0.3)
    except serial.SerialException as exc:
        sys.exit(f"error: cannot open {port}: {exc}")

    try:
        if args.verb.lower() == "console":
            return cmd_console(sp)

        replies = send(sp, build_command(args), args.timeout)
        if not replies:
            print("warning: no reply", file=sys.stderr)
            return 1
        for reply in replies:
            print(reply)
        return 1 if any(r.startswith("ERR") for r in replies) else 0
    finally:
        sp.close()


if __name__ == "__main__":
    sys.exit(main())
