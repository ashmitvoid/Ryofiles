#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/ryofiles" >&2
    exit 2
fi

binary="$1"
[[ -x "$binary" ]] || { echo "not executable: $binary" >&2; exit 2; }

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
mkdir -p "$workdir/home" "$workdir/runtime" "$workdir/config" "$workdir/cache" "$workdir/data"
chmod 700 "$workdir/runtime"

set +e
output="$(
    HOME="$workdir/home" \
    XDG_RUNTIME_DIR="$workdir/runtime" \
    XDG_CONFIG_HOME="$workdir/config" \
    XDG_CACHE_HOME="$workdir/cache" \
    XDG_DATA_HOME="$workdir/data" \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    timeout 4s "$binary" 2>&1
)"
status=$?
set -e

printf '%s\n' "$output"

if grep -Fq "QQmlApplicationEngine failed to load component" <<<"$output"; then
    echo "QML application engine failed to load the root component" >&2
    exit 1
fi

if grep -Fq "Type " <<<"$output" && grep -Fq " unavailable" <<<"$output"; then
    echo "QML type dependency was unavailable during startup" >&2
    exit 1
fi

if grep -Fq "Cannot assign to non-existent property" <<<"$output"; then
    echo "QML assigned a non-existent property during startup" >&2
    exit 1
fi

# A healthy GUI process is expected to remain alive until timeout in this headless smoke.
if [[ $status -ne 124 ]]; then
    echo "Ryofiles exited unexpectedly during startup smoke (status=$status)" >&2
    exit 1
fi

echo "Ryofiles root QML instantiated successfully"
