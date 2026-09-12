#!/usr/bin/env bash
#
# One-time root setup. Only needed for SWD debugging and flashing via pyocd --
# the mass-storage flash path in scripts/flash.sh works without any of this.
#
#   sudo ./scripts/host-setup.sh
#
set -euo pipefail

RULE=/etc/udev/rules.d/99-frdm-k64f.rules

[[ ${EUID} -eq 0 ]] || { echo "error: run me with sudo" >&2; exit 1; }

cat > "${RULE}" <<'RULES'
# NXP OpenSDA / CMSIS-DAP debug probe on the FRDM-K64F.
# uaccess grants the active seat user access immediately, without a relogin.
SUBSYSTEM=="usb", ATTR{idVendor}=="0d28", ATTR{idProduct}=="0204", \
  MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0d28", ATTRS{idProduct}=="0204", \
  MODE="0660", GROUP="plugdev", TAG+="uaccess"

# The board's own CDC console, with a stable name so it does not wander
# between ttyACM numbers as other devices come and go.
SUBSYSTEM=="tty", ATTRS{idVendor}=="0d28", ATTRS{idProduct}=="0204", \
  SYMLINK+="frdm-k64f", MODE="0660", GROUP="dialout", TAG+="uaccess", \
  ENV{ID_MM_DEVICE_IGNORE}="1"
RULES

udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --subsystem-match=tty --action=add

echo "done. replug the board, then check:"
echo "  ls -l /dev/frdm-k64f"
echo "  pyocd list"
