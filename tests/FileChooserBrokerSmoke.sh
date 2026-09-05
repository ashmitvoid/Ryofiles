#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s /path/to/ryofiles\n' "$0" >&2
  exit 2
fi

portal_bin=$(realpath "$1")
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ -z "${RYOFILES_BROKER_IN_SESSION:-}" ]]; then
  runtime=$(mktemp -d)
  cleanup_runtime() { rm -rf "$runtime"; }
  trap cleanup_runtime EXIT
  chmod 700 "$runtime"
  RYOFILES_BROKER_IN_SESSION=1 \
  XDG_RUNTIME_DIR="$runtime" \
    dbus-run-session -- bash "$0" "$portal_bin"
  exit $?
fi

frontend=/usr/lib/xdg-desktop-portal
if [[ ! -x "$frontend" ]]; then
  printf 'xdg-desktop-portal frontend not found at %s\n' "$frontend" >&2
  exit 1
fi

service_backend='org.freedesktop.impl.portal.desktop.ryofiles'
service_frontend='org.freedesktop.portal.Desktop'
tmp=$(mktemp -d)
backend_pid=''
frontend_pid=''

cleanup() {
  if [[ -n "$frontend_pid" ]] && kill -0 "$frontend_pid" 2>/dev/null; then
    kill "$frontend_pid" 2>/dev/null || true
    wait "$frontend_pid" 2>/dev/null || true
  fi
  if [[ -n "$backend_pid" ]] && kill -0 "$backend_pid" 2>/dev/null; then
    kill "$backend_pid" 2>/dev/null || true
    wait "$backend_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -p \
  "$tmp/config/xdg-desktop-portal" \
  "$tmp/data/xdg-desktop-portal/portals"

cat >"$tmp/config/xdg-desktop-portal/portals.conf" <<'EOF'
[preferred]
default=none
org.freedesktop.impl.portal.FileChooser=ryofiles
EOF

cat >"$tmp/data/xdg-desktop-portal/portals/ryofiles.portal" <<'EOF'
[portal]
DBusName=org.freedesktop.impl.portal.desktop.ryofiles
Interfaces=org.freedesktop.impl.portal.FileChooser;
EOF

export XDG_CONFIG_HOME="$tmp/config"
export XDG_CURRENT_DESKTOP=ryofiles-smoke
export XDG_DATA_HOME="$tmp/data"
export XDG_DATA_DIRS="$tmp/data:/usr/share"
unset XDG_DESKTOP_PORTAL_DIR || true

selected="$tmp/selected.txt"
printf 'broker smoke\n' >"$selected"
selected_uri="file://$selected"

fake_picker="$tmp/fake-picker"
cat >"$fake_picker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cat >/dev/null
printf '{"version":1,"uris":["%s"],"filter":-1,"choices":{}}\n' \
  "${RYOFILES_BROKER_SMOKE_URI:?missing smoke URI}"
EOF
chmod +x "$fake_picker"

name_has_owner() {
  local name=$1
  gdbus call \
    --session \
    --dest org.freedesktop.DBus \
    --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner \
    "$name" 2>/dev/null | grep -q 'true'
}

wait_for_owner() {
  local name=$1
  local i
  for i in $(seq 1 160); do
    if name_has_owner "$name"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

RYOFILES_PICKER_EXECUTABLE="$fake_picker" \
RYOFILES_BROKER_SMOKE_URI="$selected_uri" \
QT_QPA_PLATFORM=offscreen \
  "$portal_bin" --filechooser-portal \
    >"$tmp/backend.out" 2>"$tmp/backend.err" &
backend_pid=$!

if ! wait_for_owner "$service_backend"; then
  printf 'Ryofiles backend did not acquire its session bus name\n' >&2
  cat "$tmp/backend.err" >&2 || true
  exit 1
fi

"$frontend" -v >"$tmp/frontend.out" 2>"$tmp/frontend.err" &
frontend_pid=$!

if ! wait_for_owner "$service_frontend"; then
  printf 'xdg-desktop-portal did not acquire org.freedesktop.portal.Desktop\n' >&2
  cat "$tmp/frontend.out" >&2 || true
  cat "$tmp/frontend.err" >&2 || true
  exit 1
fi

if ! timeout 12s python "$script_dir/FileChooserBrokerClient.py" "$selected_uri"; then
  printf '%s\n' '--- xdg-desktop-portal stdout ---' >&2
  cat "$tmp/frontend.out" >&2 || true
  printf '%s\n' '--- xdg-desktop-portal stderr ---' >&2
  cat "$tmp/frontend.err" >&2 || true
  printf '%s\n' '--- Ryofiles backend stderr ---' >&2
  cat "$tmp/backend.err" >&2 || true
  exit 1
fi

name_has_owner "$service_frontend"
name_has_owner "$service_backend"
printf 'FileChooser public xdg-desktop-portal broker smoke passed\n'
