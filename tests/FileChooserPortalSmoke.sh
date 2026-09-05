#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s /path/to/ryofiles\n' "$0" >&2
  exit 2
fi

portal_bin=$1
service='org.freedesktop.impl.portal.desktop.ryofiles'
portal_object='/org/freedesktop/portal/desktop'
portal_iface='org.freedesktop.impl.portal.FileChooser'
request_iface='org.freedesktop.impl.portal.Request'

tmp=$(mktemp -d)
portal_pid=''
open_pid=''

cleanup() {
  if [[ -n "$open_pid" ]] && kill -0 "$open_pid" 2>/dev/null; then
    kill "$open_pid" 2>/dev/null || true
    wait "$open_pid" 2>/dev/null || true
  fi
  if [[ -n "$portal_pid" ]] && kill -0 "$portal_pid" 2>/dev/null; then
    kill "$portal_pid" 2>/dev/null || true
    wait "$portal_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

fake_picker="$tmp/fake-picker"
cat >"$fake_picker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "${RYOFILES_SMOKE_PICKER_MODE:-}" in
  success)
    printf '%s\n' "${RYOFILES_SMOKE_FILE_URI:?missing smoke URI}"
    ;;
  block)
    sleep 30
    ;;
  *)
    exit 2
    ;;
esac
EOF
chmod +x "$fake_picker"

name_has_owner() {
  gdbus call \
    --session \
    --dest org.freedesktop.DBus \
    --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner \
    "$service" 2>/dev/null | grep -q 'true'
}

wait_for_owner() {
  local expected=$1
  local i
  for i in $(seq 1 100); do
    if [[ "$expected" == yes ]]; then
      if name_has_owner; then
        return 0
      fi
    else
      if ! name_has_owner; then
        return 0
      fi
    fi
    sleep 0.05
  done
  return 1
}

start_portal() {
  local mode=$1
  local uri=${2:-}

  RYOFILES_PICKER_EXECUTABLE="$fake_picker" \
  RYOFILES_SMOKE_PICKER_MODE="$mode" \
  RYOFILES_SMOKE_FILE_URI="$uri" \
  QT_QPA_PLATFORM=offscreen \
    "$portal_bin" --filechooser-portal \
      >"$tmp/portal.out" 2>"$tmp/portal.err" &
  portal_pid=$!

  if ! wait_for_owner yes; then
    printf 'Ryofiles portal did not acquire its session bus name\n' >&2
    cat "$tmp/portal.err" >&2 || true
    return 1
  fi
}

stop_portal() {
  if [[ -n "$portal_pid" ]] && kill -0 "$portal_pid" 2>/dev/null; then
    kill "$portal_pid"
    wait "$portal_pid" 2>/dev/null || true
  fi
  portal_pid=''
  if ! wait_for_owner no; then
    printf 'Ryofiles portal bus name remained owned after process exit\n' >&2
    return 1
  fi
}

call_open_file() {
  local handle=$1
  local title=$2
  timeout 8s gdbus call \
    --session \
    --dest "$service" \
    --object-path "$portal_object" \
    --method "$portal_iface.OpenFile" \
    "$handle" \
    'org.ryoku.RyofilesSmoke' \
    '' \
    "$title" \
    '{}'
}

# Success: exercise the production backend process, its picker subprocess,
# URI parsing/validation, and the delayed D-Bus method reply.
selected="$tmp/selected.txt"
printf 'smoke\n' >"$selected"
selected_uri="file://$selected"
start_portal success "$selected_uri"

success_handle='/org/freedesktop/portal/desktop/request/ryofiles_smoke/success'
success_reply=$(call_open_file "$success_handle" 'Open smoke file')
printf '%s\n' "$success_reply" | grep -Fq 'uint32 0'
printf '%s\n' "$success_reply" | grep -Fq "$selected_uri"
name_has_owner
stop_portal

# Cancellation: leave the fake picker blocked, close the exported Request object,
# and require the pending backend FileChooser call to complete with response 2.
start_portal block
cancel_handle='/org/freedesktop/portal/desktop/request/ryofiles_smoke/cancel'
call_open_file "$cancel_handle" 'Cancel smoke file' >"$tmp/cancel.reply" &
open_pid=$!

closed=no
for _ in $(seq 1 100); do
  if gdbus call \
      --session \
      --dest "$service" \
      --object-path "$cancel_handle" \
      --method "$request_iface.Close" \
      >/dev/null 2>&1; then
    closed=yes
    break
  fi
  sleep 0.05
done

if [[ "$closed" != yes ]]; then
  printf 'Portal request object was never closable\n' >&2
  exit 1
fi

if ! wait "$open_pid"; then
  printf 'Pending OpenFile D-Bus call failed after Request.Close\n' >&2
  cat "$tmp/cancel.reply" >&2 || true
  exit 1
fi
open_pid=''

grep -Fq 'uint32 2' "$tmp/cancel.reply"
name_has_owner
stop_portal

printf 'FileChooser private-session D-Bus smoke passed\n'
