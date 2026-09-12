#!/usr/bin/env bash
#
# Install a Zephyr build environment without root.
#
#   ./scripts/install-toolchain.sh
#
# Ubuntu 24.04 ships python3 without pip and without python3-venv's ensurepip,
# so the venv is created --without-pip and pip is bootstrapped into it by hand.
# That keeps the whole toolchain in $HOME and needs no apt.
#
set -euo pipefail

VENV=${ZEPHYR_VENV:-${HOME}/zephyr-venv}
WS=${ZEPHYR_WS:-${HOME}/zephyrproject}
ZEPHYR_REV=${ZEPHYR_REV:-v4.4.2}

if [[ ! -x ${VENV}/bin/python ]]; then
  echo "creating venv at ${VENV}"
  python3 -m venv --without-pip "${VENV}"
  tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
  curl -sSfL https://bootstrap.pypa.io/get-pip.py -o "${tmp}/get-pip.py"
  "${VENV}/bin/python" "${tmp}/get-pip.py" -q
fi

export PATH="${VENV}/bin:${PATH}"

echo "installing west and the build tools"
pip install -q --upgrade west cmake ninja pyelftools pyocd

if [[ ! -d ${WS}/.west ]]; then
  echo "initialising the Zephyr workspace at ${WS} (${ZEPHYR_REV})"
  west init -m https://github.com/zephyrproject-rtos/zephyr --mr "${ZEPHYR_REV}" "${WS}"
fi

cd "${WS}"
west update --narrow -o=--depth=1
pip install -q -r "${WS}/zephyr/scripts/requirements.txt"

# The SDK installer shells out to cmake, so the venv has to be on PATH.
west sdk install -t arm-zephyr-eabi

cat <<MSG

done. add this to your shell profile, or run it before building:

  export PATH="${VENV}/bin:\$PATH"

then:  ./scripts/build.sh
MSG
