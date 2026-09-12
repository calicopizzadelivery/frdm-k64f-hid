#!/usr/bin/env bash
#
# Build the firmware.
#
#   ./scripts/build.sh            # incremental
#   ./scripts/build.sh --pristine # from scratch
#
set -euo pipefail

VENV=${ZEPHYR_VENV:-${HOME}/zephyr-venv}
WS=${ZEPHYR_WS:-${HOME}/zephyrproject}
BOARD=${BOARD:-frdm_k64f}
REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="${REPO_ROOT}/build"
PRISTINE=auto

[[ ${1:-} == --pristine ]] && PRISTINE=always

export PATH="${VENV}/bin:${PATH}"
command -v west >/dev/null || {
  echo "error: west not found. run ./scripts/install-toolchain.sh" >&2
  exit 1
}

cd "${WS}"
west build -p "${PRISTINE}" -b "${BOARD}" -d "${BUILD}" "${REPO_ROOT}/app"

echo
ls -lh "${BUILD}/zephyr/zephyr.bin" "${BUILD}/zephyr/zephyr.hex" 2>/dev/null | sed 's/^/  /'
