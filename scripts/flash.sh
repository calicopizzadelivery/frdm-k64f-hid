#!/usr/bin/env bash
#
# Flash over the OpenSDA mass-storage bootloader.
#
#   ./scripts/flash.sh
#
# The board exposes a small FAT volume labelled MBED; dropping a raw .bin on it
# programs the target and resets it. That volume unmounts across the reset, so
# a second flash in a row usually needs it remounting first -- this handles that.
#
# The alternative is SWD via pyocd (target "k64f"), which needs the udev rule
# from scripts/host-setup.sh. Mass storage needs no permissions at all.
#
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BIN="${REPO_ROOT}/build/zephyr/zephyr.bin"
LABEL=${MBED_LABEL:-MBED}

[[ -f ${BIN} ]] || { echo "error: ${BIN} missing, run ./scripts/build.sh" >&2; exit 1; }

mountpoint_for() {
  lsblk -o LABEL,MOUNTPOINT -nr | awk -v l="${LABEL}" '$1==l && $2!="" {print $2; exit}'
}

vol=$(mountpoint_for || true)
if [[ -z ${vol} ]]; then
  dev=$(lsblk -o LABEL,PATH -nr | awk -v l="${LABEL}" '$1==l {print $2; exit}')
  if [[ -z ${dev} ]]; then
    echo "error: no volume labelled ${LABEL}. is the OpenSDA port connected?" >&2
    exit 1
  fi
  echo "mounting ${dev}"
  udisksctl mount -b "${dev}" >/dev/null
  sleep 1
  vol=$(mountpoint_for || true)
  [[ -n ${vol} ]] || { echo "error: could not mount ${dev}" >&2; exit 1; }
fi

echo "copying $(basename "${BIN}") to ${vol}"
cp "${BIN}" "${vol}/"
sync
echo "programmed; the target resets on its own (give it ~10s to re-enumerate)."
