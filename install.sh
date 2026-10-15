#!/usr/bin/env bash
# Copyright (C) 2026 erixabo
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Futtasd rootként: sudo ./install.sh" >&2
    exit 1
fi

if ! grep -qi 'raspberry pi' /proc/device-tree/model 2>/dev/null; then
    echo "Ez a telepítő Raspberry Pi OS-re készült." >&2
    exit 1
fi

apt-get update
apt-get install -y build-essential git python3-pil libraspberrypi-dev

tmpdir="$(mktemp -d /tmp/pisstv-librpitx.XXXXXX)"
trap 'rm -rf "$tmpdir"' EXIT
git clone https://github.com/F5OEO/librpitx.git "$tmpdir/librpitx"
git -C "$tmpdir/librpitx" checkout --detach f01bc64bcdb6207f448379193bc0a8accb9aa22
make -C "$tmpdir/librpitx/src" -j"$(nproc)"
make -C "$tmpdir/librpitx/src" install

repo_dir="$(cd "$(dirname "$0")" && pwd)"
make -C "$repo_dir" clean all
make -C "$repo_dir" install
echo "Telepítve: /usr/local/bin/pisstv és /usr/local/bin/pisstv-prepare-image"
